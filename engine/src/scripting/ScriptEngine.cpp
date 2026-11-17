// engine/src/scripting/ScriptEngine.cpp — the ONLY TU that includes Lua/sol2.
#include "forge/scripting/ScriptEngine.hpp"
#include "forge/audio/Audio.hpp"
#include "forge/core/Log.hpp"
#include "forge/fx/Particles.hpp"
#include "forge/physics/PhysicsWorld.hpp"
#include "forge/platform/Input.hpp"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace forge {

namespace {

// Script-facing key names. Small on purpose: grows with real gameplay
// needs (VAULT's interact keys), not speculatively.
std::optional<Key> nameToKey(const std::string& name) {
    if (name == "space") {
        return Key::Space;
    }
    if (name == "shift") {
        return Key::LeftShift;
    }
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'a' && c <= 'z') { // Key values mirror GLFW: 'A'..'Z'
            return static_cast<Key>(c - 'a' + static_cast<int>(Key::A));
        }
        if (c >= '0' && c <= '9') {
            return static_cast<Key>(c - '0' + static_cast<int>(Key::D0));
        }
    }
    return std::nullopt;
}

void logScriptError(const char* context, const sol::error& err) {
    FORGE_ERROR("script {}: {}", context, err.what());
}

} // namespace

struct ScriptEngine::Impl {
    sol::state lua;
    sol::protected_function onUpdate; // refreshed after every successful load
    bool updateBroken = false;        // latch: log a runtime error ONCE, not per frame
    std::vector<std::string> includes;  // unique, in first-include order
    std::vector<std::string> including; // the include stack (cycle guard)
};

ScriptEngine::ScriptEngine() : m_impl(std::make_unique<Impl>()) {
    sol::state& lua = m_impl->lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

    // vec3: scripts speak the same math the engine does.
    lua.new_usertype<glm::vec3>("vec3", sol::call_constructor,
                                sol::constructors<glm::vec3(float, float, float)>(), "x",
                                &glm::vec3::x, "y", &glm::vec3::y, "z", &glm::vec3::z,
                                sol::meta_function::addition,
                                [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
                                sol::meta_function::subtraction,
                                [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
                                sol::meta_function::multiplication,
                                [](const glm::vec3& a, float k) { return a * k; }, "length",
                                [](const glm::vec3& a) { return glm::length(a); });

    sol::table forge = lua.create_named_table("forge");
    forge.set_function("log", [](const std::string& message) { FORGE_INFO("[lua] {}", message); });

    // forge.include(path) -> bool (ADR-031): a script module, loaded into
    // this state like a second chunk of the caller. Protected: a broken
    // module logs and returns false instead of aborting the includer. A
    // file already on the include stack is refused (cycle).
    Impl* impl = m_impl.get();
    forge.set_function("include", [impl](const std::string& path) {
        for (const std::string& open : impl->including) {
            if (open == path) {
                FORGE_ERROR("script include {}: include cycle, skipped", path);
                return false;
            }
        }
        if (std::find(impl->includes.begin(), impl->includes.end(), path) ==
            impl->includes.end()) {
            impl->includes.push_back(path); // watched even if this load fails
        }
        impl->including.push_back(path);
        const sol::protected_function_result result =
            impl->lua.safe_script_file(path, sol::script_pass_on_error);
        impl->including.pop_back();
        if (!result.valid()) {
            const sol::error err = result.get<sol::error>();
            FORGE_ERROR("script include {}: {}", path, err.what());
            return false;
        }
        FORGE_INFO("script include: {}", path);
        return true;
    });
}

const std::vector<std::string>& ScriptEngine::includedFiles() const { return m_impl->includes; }

ScriptEngine::~ScriptEngine() = default;

void ScriptEngine::bindPhysics(PhysicsWorld& physics) {
    sol::table forge = m_impl->lua["forge"];
    sol::table phys = forge.create_named("physics");

    // The VAULT primitives-to-be: spawn things, shove things, ask where
    // things are (pressure plates are "is a body's position inside my box").
    phys.set_function("spawnBox", [this, &physics](const glm::vec3& halfExtents,
                                                   const glm::vec3& position, bool dynamic,
                                                   float restitution) {
        const BodyId id = physics.addBox(
            halfExtents, position, dynamic ? BodyType::Dynamic : BodyType::Static, restitution);
        if (onBoxSpawned) {
            onBoxSpawned(id, halfExtents);
        }
        return id.value;
    });
    phys.set_function("kick", [&physics](uint32_t body, const glm::vec3& velocity) {
        physics.addLinearVelocity(BodyId{body}, velocity);
    });
    // set_velocity is ASSIGNMENT where kick is additive — kick can never zero
    // a body. Powers SPARK's catch snap (week 12): zero on contact, zero per
    // frame while carried, gentle release into a socket.
    phys.set_function("set_velocity", [&physics](uint32_t body, const glm::vec3& velocity) {
        physics.setLinearVelocity(BodyId{body}, velocity);
    });
    phys.set_function("position", [&physics](uint32_t body) {
        const glm::mat4 transform = physics.bodyTransform(BodyId{body});
        return glm::vec3(transform[3]);
    });
    // The pressure-plate primitive: ids of dynamic bodies in a region.
    phys.set_function("overlap", [&physics](const glm::vec3& center, const glm::vec3& half) {
        std::vector<uint32_t> ids;
        for (const BodyId id : physics.overlapBox(center, half)) {
            ids.push_back(id.value);
        }
        return sol::as_table(ids);
    });
    phys.set_function("velocity",
                      [&physics](uint32_t body) { return physics.linearVelocity(BodyId{body}); });
    // Returns (bodyId, distance) or nil: the laser line-of-sight query.
    phys.set_function("raycast",
                      [&physics](const glm::vec3& origin, const glm::vec3& dir,
                                 float maxDist) -> std::optional<std::tuple<uint32_t, float>> {
                          if (const auto hit = physics.raycast(origin, dir, maxDist)) {
                              return std::make_tuple(hit->body.value, hit->distance);
                          }
                          return std::nullopt;
                      });
}

void ScriptEngine::bindInput(const Input& input) {
    sol::table forge = m_impl->lua["forge"];
    sol::table in = forge.create_named("input");
    // Edge-triggered by design: scripts poll once per frame in onUpdate,
    // which is exactly the latched-press contract Input already keeps.
    // "lmb"/"rmb" join the key names in week 12 (the charge verb).
    in.set_function("pressed", [&input](const std::string& name) {
        if (name == "lmb") {
            return input.wasMousePressed(MouseButton::Left);
        }
        if (name == "rmb") {
            return input.wasMousePressed(MouseButton::Right);
        }
        const auto key = nameToKey(name);
        if (!key) {
            FORGE_WARN("[lua] unknown key name '{}'", name);
            return false;
        }
        return input.wasKeyPressed(*key);
    });
    // Held-state polling (week 12): charge timers want "is it STILL down",
    // which the edge latch deliberately doesn't answer.
    in.set_function("down", [&input](const std::string& name) {
        if (name == "lmb") {
            return input.isMouseDown(MouseButton::Left);
        }
        if (name == "rmb") {
            return input.isMouseDown(MouseButton::Right);
        }
        const auto key = nameToKey(name);
        if (!key) {
            FORGE_WARN("[lua] unknown key name '{}'", name);
            return false;
        }
        return input.isKeyDown(*key);
    });
}

void ScriptEngine::bindScene() {
    sol::table forge = m_impl->lua["forge"];
    sol::table sceneTable = forge.create_named("scene");
    sceneTable.set_function("setPosition", [this](const std::string& name, const glm::vec3& p) {
        if (onSetEntityPosition) {
            onSetEntityPosition(name, p);
        }
    });
    sceneTable.set_function("getPosition", [this](const std::string& name) {
        return onGetEntityPosition ? onGetEntityPosition(name) : glm::vec3(0.0f);
    });
    sceneTable.set_function("setScale", [this](const std::string& name, const glm::vec3& scale) {
        if (onSetEntityScale) {
            onSetEntityScale(name, scale);
        }
    });
    // forge.scene.setRotation(name, eulerDeg): character/prop orientation
    // (ADR-027). Visual transform only — colliders stay axis-aligned.
    sceneTable.set_function("setRotation",
                            [this](const std::string& name, const glm::vec3& eulerDeg) {
                                if (onSetEntityRotation) {
                                    onSetEntityRotation(name, eulerDeg);
                                }
                            });
    // forge.scene.setTexture(name, texture): the colour-language seam (week
    // 9) — doors flip red/green, plates orange/green. Texture names resolve
    // host-side; unknown names are ignored there.
    sceneTable.set_function("setTexture",
                            [this](const std::string& name, const std::string& texture) {
                                if (onSetEntityTexture) {
                                    onSetEntityTexture(name, texture);
                                }
                            });
    sceneTable.set_function("destroy", [this](const std::string& name) {
        if (onDestroyEntity) {
            onDestroyEntity(name);
        }
    });
    // spawn(..., dynamic[, mesh]): mesh is optional so every existing call
    // site keeps meaning "unit cube" (ADR-027 — the model registry seam).
    sceneTable.set_function("spawn", [this](const std::string& name, const glm::vec3& position,
                                            const glm::vec3& scale, const glm::vec3& halfExtents,
                                            const std::string& texture, bool dynamic,
                                            sol::optional<std::string> mesh,
                                            sol::optional<sol::table> opts) {
        if (!onSpawnEntity) {
            return;
        }
        SpawnOptions options;
        if (opts) {
            options.kinematic = opts->get_or("kinematic", false);
            options.lockRotation = opts->get_or("lockRotation", false);
            options.friction = opts->get_or("friction", options.friction);
            options.density = opts->get_or("density", options.density);
            options.restitution = opts->get_or("restitution", options.restitution);
            options.yawDeg = opts->get_or("yaw", options.yawDeg);
            if (const sol::optional<float> r = (*opts)["roughness"]) { // ADR-033
                options.roughness = *r;
            }
            if (const sol::optional<float> m = (*opts)["metallic"]) {
                options.metallic = *m;
            }
            const std::string collider = opts->get_or("collider", std::string("box"));
            options.meshCollider = collider == "mesh";
            if (collider != "box" && collider != "mesh") {
                FORGE_WARN("spawn '{}': unknown collider '{}', using box", name, collider);
            }
        }
        onSpawnEntity(name, position, scale, halfExtents, texture, dynamic, mesh.value_or(""),
                      options);
    });
    // ---- Upgrade pass (ADR-029): emissive/tint, body<->name, existence.
    sceneTable.set_function("setEmissive", [this](const std::string& name, const glm::vec3& e) {
        if (onSetEntityEmissive) {
            onSetEntityEmissive(name, e);
        }
    });
    sceneTable.set_function("setTint", [this](const std::string& name, const glm::vec3& t) {
        if (onSetEntityTint) {
            onSetEntityTint(name, t);
        }
    });
    // setRoughness(name, r) / setMetallic(name, m), 0..1 (ADR-033): the
    // whole entity's surface response; defaults 0.35 / 0.15.
    sceneTable.set_function("setRoughness", [this](const std::string& name, float roughness) {
        if (onSetEntityMaterial) {
            onSetEntityMaterial(name, roughness, std::nullopt);
        }
    });
    sceneTable.set_function("setMetallic", [this](const std::string& name, float metallic) {
        if (onSetEntityMaterial) {
            onSetEntityMaterial(name, std::nullopt, metallic);
        }
    });
    sceneTable.set_function("exists", [this](const std::string& name) {
        return onEntityExists ? onEntityExists(name) : false;
    });
    sceneTable.set_function("bodyName", [this](uint32_t body) {
        return onBodyName ? onBodyName(body) : std::string();
    });
    sceneTable.set_function("bodyId", [this](const std::string& name) {
        return onBodyId ? onBodyId(name) : 0U;
    });
    sceneTable.set_function("has_model", [this](const std::string& model) {
        return onHasModel ? onHasModel(model) : false;
    });
    sol::table playerTable = forge.create_named("player");
    playerTable.set_function(
        "position", [this]() { return onPlayerPosition ? onPlayerPosition() : glm::vec3(0.0f); });
    // forge.player.teleport(pos): respawn/checkpoint warp (week 6).
    playerTable.set_function("teleport", [this](const glm::vec3& position) {
        if (onSetPlayerPosition) {
            onSetPlayerPosition(position);
        }
    });
    // forge.player.look() -> eye, dir (week 12): the glove's own ray, now
    // script-visible. Two return values, Lua-style.
    playerTable.set_function("look", [this]() -> std::tuple<glm::vec3, glm::vec3> {
        if (onPlayerLook) {
            const auto [eye, dir] = onPlayerLook();
            return {eye, dir};
        }
        return {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
    });
    // forge.player.charge() -> seconds a charge-press has been held (0 =
    // idle); forge.player.set_throw_speed(v) -> the speed the NEXT release
    // throws at (host clamps, resets after the throw). Split per the seam
    // comment in ScriptEngine.hpp: host owns the edges, script owns the curve.
    playerTable.set_function("charge",
                             [this]() { return onPlayerCharge ? onPlayerCharge() : 0.0f; });
    playerTable.set_function("set_throw_speed", [this](float speed) {
        if (onSetThrowSpeed) {
            onSetThrowSpeed(speed);
        }
    });

    playerTable.set_function("push", [this](const glm::vec3& velocity) {
        if (onPushPlayer) {
            onPushPlayer(velocity);
        }
    });
    playerTable.set_function(
        "velocity", [this]() { return onPlayerVelocity ? onPlayerVelocity() : glm::vec3(0.0f); });
    playerTable.set_function("grounded",
                             [this]() { return onPlayerGrounded ? onPlayerGrounded() : true; });
    // holding() -> entity name, or nil when the glove is empty.
    playerTable.set_function("holding", [this]() -> sol::optional<std::string> {
        if (onPlayerHolding) {
            std::string name = onPlayerHolding();
            if (!name.empty()) {
                return name;
            }
        }
        return sol::nullopt;
    });
    playerTable.set_function("drop", [this]() {
        if (onPlayerDrop) {
            onPlayerDrop();
        }
    });
    // set_yaw(deg[, pitchDeg]): face a direction after a location change
    // (yaw 0 = -z; pitch defaults to level).
    playerTable.set_function("set_yaw", [this](float yawDeg, sol::optional<float> pitchDeg) {
        if (onSetPlayerYaw) {
            onSetPlayerYaw(yawDeg, pitchDeg.value_or(0.0f));
        }
    });

    // forge.game.win(): the script signals the run is complete; the host
    // raises the win screen (week 6). Game/menu state is the HOST's business.
    sol::table gameTable = forge.create_named("game");
    gameTable.set_function("win", [this]() {
        if (onWin) {
            onWin();
        }
    });
    // forge.game.save_checkpoint(n): persist run progress (week 10). The host
    // writes it to disk; on the next launch the title menu offers Continue.
    gameTable.set_function("save_checkpoint", [this](int checkpoint) {
        if (onSaveCheckpoint) {
            onSaveCheckpoint(checkpoint);
        }
    });

    // forge.game.save(key, number) / load(key): tiny persistent store (best
    // time, collectibles). Keys are [a-z0-9_]; the host owns the file.
    gameTable.set_function("save", [this](const std::string& key, double value) {
        if (onSaveValue) {
            onSaveValue(key, value);
        }
    });
    gameTable.set_function("load", [this](const std::string& key) {
        return onLoadValue ? onLoadValue(key) : 0.0;
    });
    // forge.game.dev() -> bool: FORGE_DEV is set (dev-only verbs, ADR-031).
    gameTable.set_function("dev", [this]() { return onIsDev ? onIsDev() : false; });

    // forge.render.set_light(color[, dir]): per-room mood. dir defaults to the
    // zero vector -> host keeps its current key-light direction.
    sol::table renderTable = forge.create_named("render");
    renderTable.set_function("set_light",
                             [this](const glm::vec3& color, sol::optional<glm::vec3> dir) {
                                 if (onSetLighting) {
                                     onSetLighting(color, dir.value_or(glm::vec3(0.0f)));
                                 }
                             });

    // Local lights by id, fog, exposure, bloom (upgrade pass).
    renderTable.set_function("light", [this](const std::string& id, const glm::vec3& position,
                                             const glm::vec3& color, sol::optional<float> radius) {
        if (onSetLight) {
            onSetLight(id, position, color, radius.value_or(6.0f));
        }
    });
    renderTable.set_function("clear_light", [this](const std::string& id) {
        if (onClearLight) {
            onClearLight(id);
        }
    });
    renderTable.set_function("set_fog", [this](const glm::vec3& color, float density) {
        if (onSetFog) {
            onSetFog(color, density);
        }
    });
    renderTable.set_function("set_exposure", [this](float exposure) {
        if (onSetExposure) {
            onSetExposure(exposure);
        }
    });
    renderTable.set_function("set_bloom", [this](float intensity) {
        if (onSetBloom) {
            onSetBloom(intensity);
        }
    });
    // set_ambient([sky, ground[, weight]]) (ADR-033): hemisphere ambient,
    // linear HDR (sky = normals up, ground = normals down; ground defaults
    // to sky). weight 0..1 blends from the renderer's key-derived default
    // (0) to these colours (1, the default) for cross-fades. No arguments
    // restores the default.
    renderTable.set_function("set_ambient", [this](sol::optional<glm::vec3> sky,
                                                   sol::optional<glm::vec3> ground,
                                                   sol::optional<float> weight) {
        if (onSetAmbient) {
            if (sky) {
                onSetAmbient(*sky, ground.value_or(*sky), weight.value_or(1.0f));
            } else {
                onSetAmbient(glm::vec3(0.0f), glm::vec3(0.0f), 0.0f);
            }
        }
    });
    // set_view_distance(m): the camera far plane (outdoor locations, ADR-031).
    renderTable.set_function("set_view_distance", [this](float meters) {
        if (onSetViewDistance) {
            onSetViewDistance(meters);
        }
    });
    // set_sky([{zenith=, horizon=, ground=, sun_size=, sun_intensity=,
    // sun_halo=, texture=, texture_intensity=, texture_yaw=}]) — ADR-030.
    renderTable.set_function("set_sky", [this](sol::optional<sol::table> opts) {
        SkyOptions sky;
        if (opts) {
            const auto vec = [&](const char* key) -> std::optional<glm::vec3> {
                if (const sol::optional<glm::vec3> v = (*opts)[key]) {
                    return *v;
                }
                return std::nullopt;
            };
            const auto num = [&](const char* key) -> std::optional<float> {
                if (const sol::optional<float> v = (*opts)[key]) {
                    return *v;
                }
                return std::nullopt;
            };
            sky.zenith = vec("zenith");
            sky.horizon = vec("horizon");
            sky.ground = vec("ground");
            sky.sunSize = num("sun_size");
            sky.sunIntensity = num("sun_intensity");
            sky.sunHalo = num("sun_halo");
            sky.texture = opts->get_or("texture", std::string());
            sky.textureIntensity = num("texture_intensity");
            sky.textureYaw = num("texture_yaw");
        }
        if (onSetSky) {
            onSetSky(sky);
        }
    });
    sol::table cameraTable = forge.create_named("camera");
    cameraTable.set_function("shake", [this](float amount) {
        if (onCameraShake) {
            onCameraShake(amount);
        }
    });

    // forge.hud.set_hint(text): the per-room objective line (week 7). The
    // script decides the words; the host draws them. "" clears the hint.
    sol::table hudTable = forge.create_named("hud");
    hudTable.set_function("set_hint", [this](const std::string& text) {
        if (onSetHint) {
            onSetHint(text);
        }
    });
    hudTable.set_function("toast", [this](const std::string& text, sol::optional<float> seconds) {
        if (onToast) {
            onToast(text, seconds.value_or(2.5f));
        }
    });
    hudTable.set_function("set_stat", [this](const std::string& key, const std::string& value) {
        if (onSetStat) {
            onSetStat(key, value);
        }
    });
    // forge.hud.set_reticle(mode, value): crosshair answer-back (week 12).
    // "default" + charge01 blooms the ring; "command" + intensity flashes it
    // SPARK-blue while an F command is acknowledged.
    hudTable.set_function("set_reticle", [this](const std::string& mode, float value) {
        if (onSetReticle) {
            onSetReticle(mode, value);
        }
    });
    // ---- Location/mission pass (ADR-031): interact prompt, mission panel,
    // one subtitle line, a scene fade. Words are the script's; pixels ours.
    hudTable.set_function("set_prompt", [this](const std::string& text) {
        if (onSetPrompt) {
            onSetPrompt(text);
        }
    });
    hudTable.set_function("set_mission",
                          [this](const std::string& title, sol::optional<sol::table> lines) {
                              std::vector<std::string> out;
                              if (lines) {
                                  for (std::size_t i = 1; i <= lines->size(); ++i) {
                                      const sol::optional<std::string> line = (*lines)[i];
                                      if (line) {
                                          out.push_back(*line);
                                      }
                                  }
                              }
                              if (onSetMission) {
                                  onSetMission(title, out);
                              }
                          });
    hudTable.set_function("say", [this](const std::string& speaker, const std::string& text,
                                        sol::optional<float> seconds) {
        if (onSay) {
            onSay(speaker, text, seconds.value_or(3.5f));
        }
    });
    hudTable.set_function("set_fade", [this](float alpha) {
        if (onSetFade) {
            onSetFade(alpha);
        }
    });

    // ---- Skeletal animation (ADR-030). Entities spawned from an animated
    // model own an animator; the host resolves names.
    sol::table animTable = forge.create_named("anim");
    animTable.set_function("play", [this](const std::string& entity, const std::string& clip,
                                          sol::optional<sol::table> opts) {
        AnimPlayOptions options;
        if (opts) {
            options.loop = opts->get_or("loop", options.loop);
            options.speed = opts->get_or("speed", options.speed);
            options.fade = opts->get_or("fade", options.fade);
            options.restart = opts->get_or("restart", options.restart);
        }
        return onAnimPlay ? onAnimPlay(entity, clip, options) : false;
    });
    animTable.set_function("clips", [this](const std::string& model) {
        return sol::as_table(onAnimClips ? onAnimClips(model) : std::vector<std::string>{});
    });
    animTable.set_function(
        "time", [this](const std::string& entity)
                    -> std::tuple<sol::optional<float>, sol::optional<float>> {
            if (onAnimTime) {
                if (const auto t = onAnimTime(entity)) {
                    return {t->first, t->second};
                }
            }
            return {sol::nullopt, sol::nullopt};
        });
}

void ScriptEngine::bindAudio(Audio& audio) {
    sol::table forge = m_impl->lua["forge"];
    sol::table aud = forge.create_named("audio");
    aud.set_function("play", [&audio](const std::string& path, sol::optional<float> volume,
                                      sol::optional<float> pitch) {
        audio.play(path, volume.value_or(1.0f), pitch.value_or(1.0f));
    });
    // Positional one-shot: attenuated + panned against the host's listener.
    aud.set_function("play_at", [&audio](const std::string& path, const glm::vec3& position,
                                         sol::optional<float> volume, sol::optional<float> pitch) {
        audio.playAt(path, position, volume.value_or(1.0f), pitch.value_or(1.0f));
    });
    // Persistent looping voices (ambient room tone, music bed). Returns an
    // integer handle scripts can revolume or stop; 0 means "not playing".
    aud.set_function("loop", [&audio](const std::string& path, sol::optional<float> volume) {
        return audio.playLoop(path, volume.value_or(1.0f)).value;
    });
    aud.set_function("set_volume", [&audio](uint32_t handle, float volume) {
        audio.setVolume(SoundHandle{handle}, volume);
    });
    aud.set_function("stop", [&audio](uint32_t handle) { audio.stop(SoundHandle{handle}); });
}

void ScriptEngine::bindFx(fx::ParticleEmitter& emitter) {
    sol::table forge = m_impl->lua["forge"];
    sol::table fxTable = forge.create_named("fx");
    // burst(origin, count[, color]): color is emissive HDR radiance.
    fxTable.set_function("burst", [&emitter](const glm::vec3& origin, int count,
                                             sol::optional<glm::vec3> color) {
        if (color) {
            emitter.burst(origin, count, *color);
        } else {
            emitter.burst(origin, count);
        }
    });
}

bool ScriptEngine::runFile(const std::string& path) {
    const sol::protected_function_result result =
        m_impl->lua.safe_script_file(path, sol::script_pass_on_error);
    if (!result.valid()) {
        logScriptError("load",
                       result.get<sol::error>()); // old functions stay live — hot-reload safety
        return false;
    }
    m_impl->onUpdate = m_impl->lua["onUpdate"];
    m_impl->updateBroken = false;

    const sol::protected_function onStart = m_impl->lua["onStart"];
    if (onStart.valid()) {
        const sol::protected_function_result started = onStart();
        if (!started.valid()) {
            logScriptError("onStart", started.get<sol::error>());
        }
    }
    return true;
}

bool ScriptEngine::runString(const std::string& code) {
    const sol::protected_function_result result =
        m_impl->lua.safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
        logScriptError("run", result.get<sol::error>());
        return false;
    }
    return true;
}

void ScriptEngine::update(float dtSeconds) {
    if (m_impl->updateBroken || !m_impl->onUpdate.valid()) {
        return;
    }
    const sol::protected_function_result result = m_impl->onUpdate(dtSeconds);
    if (!result.valid()) {
        logScriptError("onUpdate (hook disabled until next reload)", result.get<sol::error>());
        m_impl->updateBroken = true;
    }
}

void ScriptEngine::setGlobal(const std::string& name, int64_t value) { m_impl->lua[name] = value; }

} // namespace forge
