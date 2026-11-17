// engine/include/forge/scripting/ScriptEngine.hpp
//
// The scripting seam (ADR-018): C++ owns everything REAL (memory, GPU,
// physics bodies); Lua owns DECISIONS (when to spawn, what to kick, which
// door opens). Lua/sol2 are implementation details behind this pimpl — the
// same discipline as GLFW/Vulkan/Jolt/Assimp before them.
//
// Error policy, chosen for the hot-reload workflow: a script error NEVER
// crashes the engine. Loads and calls are protected; failures log once and
// the last successfully loaded functions stay live. Combined with the P5
// FileWatcher this means: save a broken script, see the error, fix it, save
// again — the engine never stopped running.
//
// Scope (P6): ONE scene script with onStart()/onUpdate(dt) hooks. Per-entity
// script components arrive with the editor (P7), shaped by VAULT's needs.

#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace forge {

class PhysicsWorld;
class Input;
class Audio;
struct BodyId;

namespace fx {
class ParticleEmitter;
}

// Optional trailing table on forge.scene.spawn (upgrade pass): the physical
// character of a spawned box. Defaults reproduce the pre-upgrade behaviour.
struct SpawnOptions {
    bool kinematic = false;    // scripted mover that pushes riders (platforms)
    bool lockRotation = false; // dynamic but always axis-aligned (prisms)
    float friction = 0.5f;
    float density = 1000.0f;
    float restitution = 0.4f;
    // collider = "mesh" (ADR-032): a STATIC triangle collider from the spawned
    // model's own triangles (import correction + entity scale applied, so it
    // matches what draws). halfExtents then only sizes the box fallback,
    // used (with a warning) for dynamic/kinematic spawns or a model without
    // CPU triangles.
    bool meshCollider = false;
    float yawDeg = 0.0f; // yaw = degrees about +Y at spawn: visual AND collider
};

// Optional table on forge.anim.play (ADR-030): {loop=, speed=, fade=,
// restart=}. Replaying the clip that is already playing only updates
// loop/speed unless restart=true.
struct AnimPlayOptions {
    bool loop = true;
    float speed = 1.0f;
    float fade = 0.2f; // seconds of cross-fade (0 = cut)
    bool restart = false;
};

// forge.render.set_sky(opts) (ADR-030). Every call describes the WHOLE sky:
// fields left out (nullopt) take the renderer's indoor defaults, and
// set_sky() with no table restores the default sky. `texture` names a
// texture in assets/textures (equirectangular); "" = the procedural gradient.
struct SkyOptions {
    std::optional<glm::vec3> zenith, horizon, ground; // linear HDR rgb
    std::optional<float> sunSize;      // degrees (angular radius); 0 hides the disc
    std::optional<float> sunIntensity; // x key-light colour
    std::optional<float> sunHalo;
    std::string texture;
    std::optional<float> textureIntensity;
    std::optional<float> textureYaw; // degrees
};

class ScriptEngine {
public:
    // Opens the VM with base libraries plus forge.log() and vec3.
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;
    ScriptEngine(ScriptEngine&&) = delete;
    ScriptEngine& operator=(ScriptEngine&&) = delete;

    // Optional binding modules — call what the host actually has. The
    // referenced systems must outlive this ScriptEngine.
    // forge.physics: spawnBox/kick/position — VAULT's puzzle primitives.
    void bindPhysics(PhysicsWorld& physics);
    // forge.input: pressed("space"|"e"|letters|digits) — edge-triggered.
    void bindInput(const Input& input);
    // forge.audio: play(path, volume) — fire-and-forget.
    void bindAudio(Audio& audio);
    // forge.fx: burst(vec3, count) — scripted spark/dust bursts.
    void bindFx(fx::ParticleEmitter& emitter);

    // Fired when a script spawns a body, so the host can register it for
    // drawing. Kept as a hook because rendering is the HOST's business.
    std::function<void(BodyId body, glm::vec3 halfExtents)> onBoxSpawned;

    // Week 2 scene hooks: scripts address entities BY NAME; the host owns
    // what a name means (ECS lookup + collider sync). Set before bindScene().
    std::function<void(const std::string& name, glm::vec3 position)> onSetEntityPosition;
    std::function<glm::vec3(const std::string& name)> onGetEntityPosition;
    std::function<void(const std::string& name, glm::vec3 scale)> onSetEntityScale;
    // Colour language (week 9, master-prompt law: orange=interactable,
    // red=locked, green=solved). Scripts retint entities by swapping the
    // named texture; the host owns what texture names mean. This is the
    // affordable stand-in until HDR + emissive materials land.
    std::function<void(const std::string& name, const std::string& texture)> onSetEntityTexture;
    // Character/prop orientation (ADR-027): drone banking, eye tracking,
    // machinery. Euler degrees; the host applies it to the VISUAL transform
    // only — box colliders stay axis-aligned, so rotate colliderless parts.
    std::function<void(const std::string& name, glm::vec3 eulerDeg)> onSetEntityRotation;
    std::function<void(const std::string& name)> onDestroyEntity; // glass shatters
    // Rooms are Lua data (week 3): scripts spawn NAMED, textured, collidable
    // entities; the host owns meshes/textures/ECS. halfExtents 0 = no body.
    // mesh picks from the host's model registry (ADR-027); the empty string
    // means the unit cube, and unknown names fall back to it host-side.
    std::function<void(const std::string& name, glm::vec3 position, glm::vec3 scale,
                       glm::vec3 halfExtents, const std::string& texture, bool dynamic,
                       const std::string& mesh, const SpawnOptions& options)>
        onSpawnEntity;

    // ---- Upgrade pass seams (ADR-029) ------------------------------------
    // Emissive + tint: the REAL carrier of the colour language now that the
    // renderer has HDR + bloom. emissive is linear HDR radiance (> 1 blooms).
    std::function<void(const std::string& name, glm::vec3 emissive)> onSetEntityEmissive;
    std::function<void(const std::string& name, glm::vec3 tint)> onSetEntityTint;
    std::function<bool(const std::string& name)> onEntityExists;
    // Body <-> name: raycasts return body ids; puzzles think in names. Also
    // retires the fragile "overlap a box and hope it is unambiguous" lookup.
    std::function<std::string(uint32_t body)> onBodyName; // "" if unknown
    std::function<uint32_t(const std::string& name)> onBodyId; // 0 if none
    // ADR-031: is this name in the model registry (dressing that degrades
    // when an asset fails to import, instead of drawing the cube fallback)?
    std::function<bool(const std::string& model)> onHasModel;
    // Player seams: conveyors push, hazards read velocity, SPARK needs to
    // know what the glove holds (and can make it let go).
    std::function<void(glm::vec3 velocity)> onPushPlayer;
    std::function<glm::vec3()> onPlayerVelocity;
    std::function<bool()> onPlayerGrounded;
    std::function<std::string()> onPlayerHolding; // entity name or ""
    std::function<void()> onPlayerDrop;
    // Local lights by id (the host picks the nearest N each frame), fog,
    // exposure, bloom — the per-room mood kit.
    std::function<void(const std::string& id, glm::vec3 position, glm::vec3 color, float radius)>
        onSetLight;
    std::function<void(const std::string& id)> onClearLight;
    std::function<void(glm::vec3 color, float density)> onSetFog;
    std::function<void(float exposure)> onSetExposure;
    std::function<void(float intensity)> onSetBloom;
    std::function<void(const SkyOptions& sky)> onSetSky; // ADR-030
    // HUD: transient toasts ("DATA CORE 3/8") and named stat lines the host
    // shows on the pause/win screens. Camera shake for impacts.
    std::function<void(const std::string& text, float seconds)> onToast;
    std::function<void(const std::string& key, const std::string& value)> onSetStat;
    std::function<void(float amount)> onCameraShake;
    // Generic persistence (best time, cores): numbers by key in settings.ini.
    std::function<void(const std::string& key, double value)> onSaveValue;
    std::function<double(const std::string& key)> onLoadValue; // 0 if absent
    std::function<glm::vec3()> onPlayerPosition;
    // Respawn/checkpoint (week 6): scripts warp the player; the host owns the
    // character controller. Pairs with onPlayerPosition.
    std::function<void(glm::vec3 position)> onSetPlayerPosition;
    // Aim seam (week 12): eye position + look direction — the same ray the
    // glove uses. Powers SPARK's Go-There command and any aimed verb.
    std::function<std::pair<glm::vec3, glm::vec3>()> onPlayerLook;
    // Charged launch (week 12). The HOST owns the press-edge disambiguation
    // (a grab-press must never throw on release; a charge-press throws on
    // release): charge() reports seconds since a charge-press began, 0 when
    // idle. The SCRIPT owns the charge CURVE — it maps that time to a speed
    // via set_throw_speed (host clamps, and resets to the 11 m/s legacy tap
    // speed after every throw), keeping feel tuning hot-reloadable.
    std::function<float()> onPlayerCharge;
    std::function<void(float speed)> onSetThrowSpeed;
    // Run complete (week 6): the script fires this once the finale is cleared
    // so the host can raise the win screen. Host owns the menu/game state.
    std::function<void()> onWin;
    // Save seam (week 10): the script reports the furthest checkpoint; the
    // host persists it (settings.ini) and offers Continue on the title menu.
    // The SCRIPT owns what a checkpoint number means; the host just stores it.
    std::function<void(int checkpoint)> onSaveCheckpoint;
    // Per-room lighting pass (week 5): scripts set the key-light colour (rgb *
    // HDR intensity) and direction by room; the host owns the renderer. When
    // direction is the zero vector the host keeps its current direction.
    std::function<void(glm::vec3 color, glm::vec3 direction)> onSetLighting;
    // Objective hint line (week 7): the script owns WHAT the hint says (level
    // knowledge); the host owns drawing it on the HUD. Empty string clears.
    std::function<void(const std::string& text)> onSetHint;
    // Reticle seam (week 12): the crosshair answers back. mode "default"
    // blooms with value = charge01; mode "command" is the blue F-command
    // acknowledgment with value = intensity. The host owns the pixels.
    std::function<void(const std::string& mode, float value)> onSetReticle;
    // Skeletal animation (ADR-030): forge.anim.play(entity, clip[, opts]) ->
    // bool (false: no animator or unknown clip), forge.anim.clips(model) ->
    // list of clip names, forge.anim.time(entity) -> seconds, duration (nil
    // when the entity plays nothing).
    std::function<bool(const std::string& entity, const std::string& clip,
                       const AnimPlayOptions& options)>
        onAnimPlay;
    std::function<std::vector<std::string>(const std::string& model)> onAnimClips;
    std::function<std::optional<std::pair<float, float>>(const std::string& entity)> onAnimTime;
    // ---- Location/mission pass (ADR-031). The script owns the words and
    // when they show; the host owns the pixels. "" (or an empty title)
    // clears. set_mission lines starting "[x] " draw as done, "[ ] " open.
    std::function<void(const std::string& text)> onSetPrompt; // "E  Talk"
    std::function<void(const std::string& title, const std::vector<std::string>& lines)>
        onSetMission;
    std::function<void(const std::string& speaker, const std::string& text, float seconds)>
        onSay;                                     // one subtitle line, bottom-centre
    std::function<void(float alpha)> onSetFade;    // 0..1 black over the scene
    std::function<bool()> onIsDev;                 // FORGE_DEV set (dev tools)
    std::function<void(float meters)> onSetViewDistance; // camera far plane
    std::function<void(float yawDeg, float pitchDeg)> onSetPlayerYaw; // 0 = facing -z
    // forge.scene.setPosition/getPosition(name), forge.player.position(),
    // forge.render.set_light(color, dir), forge.hud.set_hint(text).
    void bindScene();

    // Protected load/run. Returns false (and logs) on failure; previously
    // loaded functions remain live. Calls onStart() if the script defines it.
    bool runFile(const std::string& path);
    bool runString(const std::string& code); // headless smoke / console use

    // Script modules (ADR-031): forge.include(path) runs another file into
    // the SAME state, protected (an error logs and returns false; the
    // including script carries on). Every path ever included is listed
    // here (unique, first-include order) so the host can watch it.
    [[nodiscard]] const std::vector<std::string>& includedFiles() const;

    // Calls the script's onUpdate(dt) if defined. Errors log ONCE per load
    // (not once per frame) and disable the hook until the next runFile.
    void update(float dtSeconds);

    // Expose a named integer to scripts (body handles, config).
    void setGlobal(const std::string& name, int64_t value);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
