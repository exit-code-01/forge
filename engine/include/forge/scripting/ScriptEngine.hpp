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
#include <string>

namespace forge {

class PhysicsWorld;
class Input;
class Audio;
struct BodyId;

namespace fx {
class ParticleEmitter;
}

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
                       const std::string& mesh)>
        onSpawnEntity;
    std::function<glm::vec3()> onPlayerPosition;
    // Respawn/checkpoint (week 6): scripts warp the player; the host owns the
    // character controller. Pairs with onPlayerPosition.
    std::function<void(glm::vec3 position)> onSetPlayerPosition;
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
    // forge.scene.setPosition/getPosition(name), forge.player.position(),
    // forge.render.set_light(color, dir), forge.hud.set_hint(text).
    void bindScene();

    // Protected load/run. Returns false (and logs) on failure; previously
    // loaded functions remain live. Calls onStart() if the script defines it.
    bool runFile(const std::string& path);
    bool runString(const std::string& code); // headless smoke / console use

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
