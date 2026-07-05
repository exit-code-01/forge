// engine/src/scripting/ScriptEngine.cpp — the ONLY TU that includes Lua/sol2.
#include "forge/scripting/ScriptEngine.hpp"
#include "forge/audio/Audio.hpp"
#include "forge/core/Log.hpp"
#include "forge/fx/Particles.hpp"
#include "forge/physics/PhysicsWorld.hpp"
#include "forge/platform/Input.hpp"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

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
};

ScriptEngine::ScriptEngine() : m_impl(std::make_unique<Impl>()) {
    sol::state& lua = m_impl->lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

    // vec3: scripts speak the same math the engine does.
    lua.new_usertype<glm::vec3>("vec3", sol::call_constructor,
                                sol::constructors<glm::vec3(float, float, float)>(), "x",
                                &glm::vec3::x, "y", &glm::vec3::y, "z", &glm::vec3::z);

    sol::table forge = lua.create_named_table("forge");
    forge.set_function("log", [](const std::string& message) { FORGE_INFO("[lua] {}", message); });
}

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
    in.set_function("pressed", [&input](const std::string& name) {
        const auto key = nameToKey(name);
        if (!key) {
            FORGE_WARN("[lua] unknown key name '{}'", name);
            return false;
        }
        return input.wasKeyPressed(*key);
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
    sceneTable.set_function("destroy", [this](const std::string& name) {
        if (onDestroyEntity) {
            onDestroyEntity(name);
        }
    });
    sceneTable.set_function("spawn", [this](const std::string& name, const glm::vec3& position,
                                            const glm::vec3& scale, const glm::vec3& halfExtents,
                                            const std::string& texture, bool dynamic) {
        if (onSpawnEntity) {
            onSpawnEntity(name, position, scale, halfExtents, texture, dynamic);
        }
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

    // forge.game.win(): the script signals the run is complete; the host
    // raises the win screen (week 6). Game/menu state is the HOST's business.
    sol::table gameTable = forge.create_named("game");
    gameTable.set_function("win", [this]() {
        if (onWin) {
            onWin();
        }
    });

    // forge.render.set_light(color[, dir]): per-room mood. dir defaults to the
    // zero vector -> host keeps its current key-light direction.
    sol::table renderTable = forge.create_named("render");
    renderTable.set_function(
        "set_light", [this](const glm::vec3& color, sol::optional<glm::vec3> dir) {
            if (onSetLighting) {
                onSetLighting(color, dir.value_or(glm::vec3(0.0f)));
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
}

void ScriptEngine::bindAudio(Audio& audio) {
    sol::table forge = m_impl->lua["forge"];
    sol::table aud = forge.create_named("audio");
    aud.set_function("play", [&audio](const std::string& path, sol::optional<float> volume) {
        audio.play(path, volume.value_or(1.0f));
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
    fxTable.set_function(
        "burst", [&emitter](const glm::vec3& origin, int count) { emitter.burst(origin, count); });
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
