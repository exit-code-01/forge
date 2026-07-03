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
    std::function<glm::vec3()> onPlayerPosition;
    // forge.scene.setPosition/getPosition(name), forge.player.position().
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
