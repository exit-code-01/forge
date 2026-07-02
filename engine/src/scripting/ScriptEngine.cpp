// engine/src/scripting/ScriptEngine.cpp — the ONLY TU that includes Lua/sol2.
#include "forge/scripting/ScriptEngine.hpp"
#include "forge/core/Log.hpp"
#include "forge/physics/PhysicsWorld.hpp"
#include "forge/platform/Input.hpp"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cctype>
#include <optional>
#include <utility>

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
