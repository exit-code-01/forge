// sandbox/src/main.cpp — P2 demo
// Order matters: ECS first (works on ANY machine, even headless CI), then the
// window (headless CI dies here, gracefully), then the renderer — which is
// OPTIONAL: no Vulkan means warn and keep running windowed.
// Window must precede VulkanContext: the surface extensions come from GLFW.

#include "forge/anim/Clip.hpp"
#include "forge/assets/Assets.hpp"
#include "forge/audio/Audio.hpp"
#include "forge/ecs/Registry.hpp"
#include "forge/forge.hpp"
#include "forge/fx/Particles.hpp"
#include "forge/physics/PhysicsWorld.hpp"
#include "forge/platform/Window.hpp"
#include "forge/renderer/Renderer.hpp"
#include "forge/renderer/VulkanContext.hpp"
#include "forge/scripting/ScriptEngine.hpp"
#include "forge/ui/EditorUi.hpp"

#include <imgui.h>

// ImGuizmo requires imgui.h BEFORE it (it includes nothing itself) — the
// blank line keeps clang-format from alphabetizing them into a broken order.
#include <ImGuizmo.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Transform {
    glm::vec3 position{0.0f};
};
struct Velocity {
    glm::vec3 value{0.0f};
};

void ecsDemo() {
    forge::ecs::Registry registry;

    const auto a = registry.create();
    const auto b = registry.create();
    const auto c = registry.create();
    registry.emplace<Transform>(a);
    registry.emplace<Velocity>(a, glm::vec3{1.0f, 0.0f, 0.0f});
    registry.emplace<Transform>(b);
    registry.emplace<Velocity>(b, glm::vec3{0.0f, 2.0f, 0.0f});
    registry.emplace<Transform>(c); // no Velocity: must be skipped by the view

    constexpr float dt = 0.1f;
    for (int step = 0; step < 3; ++step) {
        registry.each<Transform, Velocity>(
            [](forge::ecs::Entity, Transform& t, Velocity& v) { t.position += v.value * dt; });
    }
    registry.each<Transform>([](forge::ecs::Entity e, Transform& t) {
        FORGE_INFO("entity {}v{}: pos ({:.1f}, {:.1f}, {:.1f})", e.index, e.generation,
                   t.position.x, t.position.y, t.position.z);
    });

    // Generation safety, demonstrated: destroy b, recycle its slot.
    registry.destroy(b);
    const auto d = registry.create(); // same index as b, NEW generation
    FORGE_INFO("recycled slot: b was {}v{}, d is {}v{}, alive(b)={}", b.index, b.generation,
               d.index, d.generation, registry.alive(b));
}

// Headless physics smoke: proves Jolt init + simulation on ANY machine,
// including CI runners that never get past glfwInit. A cube dropped from
// 3 m must fall and come to rest on the slab (top at y = -0.75, so the
// cube's center settles near -0.25).
void physicsDemo() {
    forge::PhysicsWorld physics;
    physics.addBox({4.0f, 0.1f, 4.0f}, {0.0f, -0.85f, 0.0f}, forge::BodyType::Static);
    const auto cube =
        physics.addBox({0.5f, 0.5f, 0.5f}, {0.0f, 3.0f, 0.0f}, forge::BodyType::Dynamic, 0.4f);

    // Feed update() frame-sized time slices, like a real game loop would —
    // big gulps hit the anti-spiral clamp by design and simulate LESS time
    // than you asked for (found out the honest way; see the P4 devlog).
    const auto simulate = [&physics](float seconds) {
        constexpr float kFrame = 1.0f / 60.0f;
        for (float t = 0.0f; t < seconds; t += kFrame) {
            physics.update(kFrame);
        }
    };

    const float y0 = physics.bodyTransform(cube)[3].y;
    simulate(0.75f); // mid-fall
    const float yMid = physics.bodyTransform(cube)[3].y;
    simulate(3.0f); // bounced and settled
    const float yRest = physics.bodyTransform(cube)[3].y;
    FORGE_INFO("physics drop: y start {:.2f} -> mid-fall {:.2f} -> rest {:.2f}", y0, yMid, yRest);
}

// Headless asset smoke: importers must work on ANY machine, including CI —
// these run before the window and fail LOUDLY if the committed assets or
// the import path regress. Loaders log their own success lines.
void assetDemo() {
    const auto torus = forge::assets::loadMesh("assets/models/torus.obj");
    const auto crate = forge::assets::loadImage("assets/textures/crate.png");
    if (torus.indices.empty() || crate.rgba.empty()) {
        throw std::runtime_error("asset smoke: importer returned empty data");
    }
}

// Unit cube, 24 vertices (4 per face so normals are hard), 36 indices.
// Faces are wound CCW seen from OUTSIDE; u cross v == n keeps that true.
void appendFace(std::vector<forge::Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 n,
                glm::vec3 u, glm::vec3 v) {
    const auto base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({(n - u - v) * 0.5f, n, {0.0f, 1.0f}});
    vertices.push_back({(n + u - v) * 0.5f, n, {1.0f, 1.0f}});
    vertices.push_back({(n + u + v) * 0.5f, n, {1.0f, 0.0f}});
    vertices.push_back({(n - u + v) * 0.5f, n, {0.0f, 0.0f}});
    indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
}

void buildCube(std::vector<forge::Vertex>& vertices, std::vector<uint32_t>& indices) {
    const glm::vec3 x{1, 0, 0};
    const glm::vec3 y{0, 1, 0};
    const glm::vec3 z{0, 0, 1};
    appendFace(vertices, indices, z, x, y);   // +Z
    appendFace(vertices, indices, -z, -x, y); // -Z
    appendFace(vertices, indices, x, -z, y);  // +X
    appendFace(vertices, indices, -x, z, y);  // -X
    appendFace(vertices, indices, y, x, -z);  // +Y
    appendFace(vertices, indices, -y, x, z);  // -Y
}

// ---- Scene components (P7.2): the scene IS the ECS now. The editor's
// hierarchy and inspector operate on these; the renderer consumes the
// DrawItems built from them each frame.
struct Name {
    std::string value;
};
struct TransformC {
    glm::vec3 position{0.0f};
    glm::vec3 eulerDeg{0.0f}; // yaw/pitch/roll editing units; radians internally
    glm::vec3 scale{1.0f};
};
struct MeshRendererC {
    forge::MeshHandle mesh;
    forge::TextureHandle texture;
};
struct BodyC {
    forge::BodyId id{};
    bool dynamic = false;
};

glm::mat4 trs(const TransformC& t) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position);
    m = glm::rotate(m, glm::radians(t.eulerDeg.y), {0.0f, 1.0f, 0.0f});
    m = glm::rotate(m, glm::radians(t.eulerDeg.x), {1.0f, 0.0f, 0.0f});
    m = glm::rotate(m, glm::radians(t.eulerDeg.z), {0.0f, 0.0f, 1.0f});
    return glm::scale(m, t.scale);
}

// Headless scripting smoke: VM boot + a C++ <-> Lua round trip, no window
// needed — CI exercises Lua/sol2 on all three OSes.
void scriptDemo() {
    forge::ScriptEngine script;
    script.setGlobal("answer", 21);
    const bool ok =
        script.runString("forge.log('hello from ' .. _VERSION .. ', answer*2 = ' .. (answer * 2))");
    if (!ok) {
        throw std::runtime_error("script smoke failed");
    }
}

// ---- Week 10 (ship): the user's knobs + run progress, one tiny ini.
// key=value lines, no sections, unknown keys ignored — deliberately dumb.
struct GameSettings {
    float mouseSensitivity = 0.14f;
    bool invertY = false;
    float masterVolume = 1.0f;
    int savedCheckpoint = 1; // 1 == the start; >1 offers Continue on the title
};

constexpr const char* kSettingsPath = "settings.ini";

GameSettings loadSettings(const char* path) {
    GameSettings s;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        try {
            if (key == "sensitivity") {
                s.mouseSensitivity = std::clamp(std::stof(value), 0.02f, 0.5f);
            } else if (key == "invert_y") {
                s.invertY = value == "1";
            } else if (key == "volume") {
                s.masterVolume = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "checkpoint") {
                s.savedCheckpoint = std::max(1, std::stoi(value));
            }
        } catch (const std::exception&) {
            // Malformed value: keep the default. Settings must never crash.
        }
    }
    return s;
}

void saveSettings(const char* path, const GameSettings& s) {
    std::ofstream out(path, std::ios::trunc);
    out << "sensitivity=" << s.mouseSensitivity << "\n"
        << "invert_y=" << (s.invertY ? 1 : 0) << "\n"
        << "volume=" << s.masterVolume << "\n"
        << "checkpoint=" << s.savedCheckpoint << "\n";
}

// Hot reload (P5.3): poll a file's mtime; on change, run the reload action.
// Polling beats OS file-watch APIs here: 3 platforms, ~zero code, and a
// half-second latency nobody perceives. A failed load is NOT fatal — tools
// write files non-atomically, so we may catch a half-written file; warn and
// retry on the next poll.
class FileWatcher {
public:
    explicit FileWatcher(std::filesystem::path path) : m_path(std::move(path)) {
        std::error_code ec;
        m_lastWrite = std::filesystem::last_write_time(m_path, ec);
    }

    template <typename Fn> void poll(Fn&& reload) {
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(m_path, ec);
        if (ec || stamp == m_lastWrite) {
            return;
        }
        try {
            reload(m_path.string());
            m_lastWrite = stamp;
        } catch (const std::exception& e) {
            FORGE_WARN("hot reload deferred ({}): {}", m_path.string(), e.what());
        }
    }

private:
    std::filesystem::path m_path;
    std::filesystem::file_time_type m_lastWrite;
};

} // namespace

int main() {
    const auto v = forge::version();
    FORGE_INFO("Forge Engine v{}.{}.{}", v.major, v.minor, v.patch);

    ecsDemo();
    physicsDemo();
    try {
        assetDemo();
        scriptDemo();
    } catch (const std::exception& e) {
        FORGE_ERROR("fatal: {}", e.what());
        return 1; // broken assets/scripting are a real failure, headless or not
    }

    try {
        forge::Window window({.title = "Forge Sandbox", .width = 1280, .height = 720});

        // Declaration order == destruction order: renderer dies before the
        // context, context before the window. Do not reorder.
        std::unique_ptr<forge::VulkanContext> vulkan;
        std::unique_ptr<forge::Renderer> renderer;
        std::unique_ptr<forge::EditorUi> ui; // declared AFTER renderer: dies first
        try {
            vulkan = std::make_unique<forge::VulkanContext>(window.requiredVulkanExtensions());
            renderer = std::make_unique<forge::Renderer>(window, *vulkan);
            ui = std::make_unique<forge::EditorUi>(window, *renderer);
        } catch (const std::exception& e) {
            FORGE_WARN("renderer unavailable: {} — running windowed without rendering", e.what());
        }

        forge::MeshHandle cubeMesh{};
        forge::MeshHandle torusMesh{};
        forge::TextureHandle crateTexture{};
        forge::TextureHandle laserTexture{};
        forge::TextureHandle glassTexture{};
        forge::TextureHandle concreteTexture{};
        forge::TextureHandle floorTexture{};
        forge::TextureHandle metalTexture{};
        // Script-facing texture registry: scene.lua spawns and RETINTS by
        // name (colour language, week 9). The host owns what a name means.
        std::unordered_map<std::string, forge::TextureHandle> textureByName;
        if (renderer) {
            std::vector<forge::Vertex> vertices;
            std::vector<uint32_t> indices;
            buildCube(vertices, indices);
            cubeMesh = renderer->addMesh(vertices, indices);

            // The import path, end to end: file -> CPU data -> GPU handles.
            const auto torusData = forge::assets::loadMesh("assets/models/torus.obj");
            torusMesh = renderer->addMesh(torusData.vertices, torusData.indices);
            const auto crateImage = forge::assets::loadImage("assets/textures/crate.png");
            crateTexture =
                renderer->addTexture(crateImage.width, crateImage.height, crateImage.rgba);
            const auto laserImage = forge::assets::loadImage("assets/textures/laser.png");
            laserTexture =
                renderer->addTexture(laserImage.width, laserImage.height, laserImage.rgba);
            const auto glassImage = forge::assets::loadImage("assets/textures/glass.png");
            glassTexture =
                renderer->addTexture(glassImage.width, glassImage.height, glassImage.rgba);
            const auto concreteImage = forge::assets::loadImage("assets/textures/concrete.png");
            concreteTexture =
                renderer->addTexture(concreteImage.width, concreteImage.height, concreteImage.rgba);
            const auto floorImage = forge::assets::loadImage("assets/textures/floor.png");
            floorTexture =
                renderer->addTexture(floorImage.width, floorImage.height, floorImage.rgba);
            const auto metalImage = forge::assets::loadImage("assets/textures/metal.png");
            metalTexture =
                renderer->addTexture(metalImage.width, metalImage.height, metalImage.rgba);

            textureByName = {{"crate", crateTexture},       {"laser", laserTexture},
                             {"glass", glassTexture},       {"concrete", concreteTexture},
                             {"floor", floorTexture},       {"metal", metalTexture}};
            // Colour-language tints (week 9): red = locked, green = solved,
            // orange = interactable. Script-only names, no dedicated handles.
            for (const char* name : {"metal_red", "metal_green", "metal_orange"}) {
                const auto img =
                    forge::assets::loadImage(std::string("assets/textures/") + name + ".png");
                textureByName[name] = renderer->addTexture(img.width, img.height, img.rgba);
            }
        }

        // Physics owns WHERE dynamic things are; the ECS owns WHAT exists.
        forge::PhysicsWorld physics;
        forge::ecs::Registry scene;

        // halfExtents == 0 means "no collider".
        std::function<void(uint32_t, forge::ecs::Entity)> spawnRegisterHook;
        const auto spawnEntity = [&](std::string name, glm::vec3 position, glm::vec3 scale,
                                     forge::MeshHandle mesh, forge::TextureHandle texture,
                                     glm::vec3 halfExtents, bool dynamic,
                                     float restitution = 0.3f) {
            const auto e = scene.create();
            scene.emplace<Name>(e, Name{std::move(name)});
            auto& t = scene.emplace<TransformC>(e);
            t.position = position;
            t.scale = scale;
            scene.emplace<MeshRendererC>(e, MeshRendererC{mesh, texture});
            if (halfExtents != glm::vec3(0.0f)) {
                const auto body = physics.addBox(
                    halfExtents, position,
                    dynamic ? forge::BodyType::Dynamic : forge::BodyType::Static, restitution);
                scene.emplace<BodyC>(e, BodyC{body, dynamic});
                if (spawnRegisterHook) {
                    spawnRegisterHook(body.value, e);
                }
            }
            return e;
        };

        // ---- VAULT week 1: the grey-box tutorial room. Floor top at y=0.
        const auto checker = forge::Renderer::defaultTexture();
        spawnEntity("Floor", {0.0f, -0.2f, 0.0f}, {14.0f, 0.4f, 14.0f}, cubeMesh, concreteTexture,
                    {7.0f, 0.2f, 7.0f}, false);
        // North wall has the DOORWAY: two segments + a sliding Exit Door.
        spawnEntity("Wall NW", {-4.25f, 1.6f, -7.2f}, {6.3f, 3.6f, 0.4f}, cubeMesh, concreteTexture,
                    {3.15f, 1.8f, 0.2f}, false);
        spawnEntity("Wall NE", {4.25f, 1.6f, -7.2f}, {6.3f, 3.6f, 0.4f}, cubeMesh, concreteTexture,
                    {3.15f, 1.8f, 0.2f}, false);
        spawnEntity("Wall N Top", {0.0f, 3.0f, -7.2f}, {2.2f, 0.8f, 0.4f}, cubeMesh,
                    concreteTexture, {1.1f, 0.4f, 0.2f}, false);
        // Door is 0.32 thick vs the 0.4 wall: recessed 4 cm in its frame so an
        // OPEN door's faces never sit coplanar with "Wall N Top" (z-fighting).
        spawnEntity("Exit Door", {0.0f, 1.25f, -7.2f}, {2.2f, 2.5f, 0.32f}, cubeMesh, crateTexture,
                    {1.1f, 1.25f, 0.16f}, false);
        spawnEntity("Exit Pad", {0.0f, -0.2f, -8.6f}, {3.0f, 0.4f, 2.6f}, cubeMesh, concreteTexture,
                    {1.5f, 0.2f, 1.3f}, false);
        // Glass pane just behind the exit door: throw a crate through it.
        spawnEntity("Glass", {0.0f, 1.05f, -7.9f}, {2.2f, 2.1f, 0.12f}, cubeMesh, glassTexture,
                    {1.1f, 1.05f, 0.06f}, false);
        // Laser circuit across the room at chest height (indicator for now;
        // rooms 1-3 wire it into puzzles). Beam is visual-only, Lua-driven.
        spawnEntity("Laser Emitter", {-6.9f, 1.1f, -5.0f}, {0.3f, 0.3f, 0.3f}, cubeMesh,
                    laserTexture, {0.15f, 0.15f, 0.15f}, false);
        spawnEntity("Laser Receiver", {6.9f, 1.1f, -5.0f}, {0.3f, 0.3f, 0.3f}, cubeMesh,
                    laserTexture, {0.15f, 0.15f, 0.15f}, false);
        spawnEntity("Laser Beam", {0.0f, 1.1f, -5.0f}, {13.2f, 0.05f, 0.05f}, cubeMesh,
                    laserTexture, glm::vec3(0.0f), false); // no collider
        spawnEntity("Wall S", {0.0f, 1.6f, 7.2f}, {14.8f, 3.6f, 0.4f}, cubeMesh, concreteTexture,
                    {7.4f, 1.8f, 0.2f}, false);
        spawnEntity("Wall E", {7.2f, 1.6f, 0.0f}, {0.4f, 3.6f, 14.8f}, cubeMesh, concreteTexture,
                    {0.2f, 1.8f, 7.4f}, false);
        spawnEntity("Wall W", {-7.2f, 1.6f, 0.0f}, {0.4f, 3.6f, 14.8f}, cubeMesh, concreteTexture,
                    {0.2f, 1.8f, 7.4f}, false);
        // Step (walkable, tests step-offset) and ledge (needs the elevator).
        spawnEntity("Step", {3.0f, 0.15f, 2.0f}, {2.4f, 0.3f, 2.4f}, cubeMesh, concreteTexture,
                    {1.2f, 0.15f, 1.2f}, false);
        spawnEntity("Ledge", {5.5f, 1.1f, -5.5f}, {3.0f, 2.2f, 3.0f}, cubeMesh, concreteTexture,
                    {1.5f, 1.1f, 1.5f}, false);
        // Throwables — Crate A sits dead ahead of the spawn look direction.
        spawnEntity("Crate A", {0.0f, 0.4f, 2.5f}, {0.5f, 0.5f, 0.5f}, cubeMesh, crateTexture,
                    {0.25f, 0.25f, 0.25f}, true, 0.3f);
        spawnEntity("Crate B", {-1.2f, 0.4f, 1.2f}, {0.5f, 0.5f, 0.5f}, cubeMesh, crateTexture,
                    {0.25f, 0.25f, 0.25f}, true, 0.3f);
        spawnEntity("Crate C", {1.3f, 0.4f, 0.8f}, {0.5f, 0.5f, 0.5f}, cubeMesh, crateTexture,
                    {0.25f, 0.25f, 0.25f}, true, 0.3f);
        // Tutorial pressure plate (scene.lua animates it sinking) + the mascot.
        spawnEntity("Plate T", {-3.0f, 0.05f, 2.0f}, {1.4f, 0.1f, 1.4f}, cubeMesh, crateTexture,
                    {0.7f, 0.05f, 0.7f}, false);
        spawnEntity("Torus", {-3.5f, 0.27f, -3.5f}, {1.0f, 1.0f, 1.0f}, torusMesh, crateTexture,
                    {0.85f, 0.25f, 0.85f}, false);

        forge::Audio audio;
        forge::fx::ParticleEmitter sparks;

        // The elevator: keyframe-animated platform (P8) rides up to the ledge.
        const auto platformEntity =
            spawnEntity("Elevator", {3.2f, 0.15f, -5.5f}, {1.8f, 0.3f, 1.8f}, cubeMesh,
                        crateTexture, {0.9f, 0.15f, 0.9f}, false);
        forge::anim::Clip platformClip;
        platformClip.positionKeys = {
            {0.0f, {3.2f, 0.15f, -5.5f}},
            {3.0f, {3.2f, 2.05f, -5.5f}},
            {4.5f, {3.2f, 2.05f, -5.5f}}, // dwell at the top
            {7.5f, {3.2f, 0.15f, -5.5f}},
        };
        float animTime = 0.0f;

        // ---- The player: capsule controller + first-person camera.
        physics.createCharacter({0.0f, 1.0f, 5.0f}, 0.35f, 0.55f);
        struct Player {
            float yaw = 0.0f; // 0 faces -Z (toward the room)
            float pitch = 0.0f;
            forge::BodyId held{};
            bool holding = false;
            bool wasGrounded = true;
            float stepTimer = 0.0f;
        } player;
        bool playMode = true; // TAB toggles into the editor

        // ---- Game state (week 6): a tiny menu FSM layered over play mode.
        // Title on launch; Esc pauses; the script signals Won. Gameplay sim
        // only advances in Playing — the other states freeze the world behind
        // the menu. The editor (TAB) is orthogonal: it's a dev tool, not a
        // game state, and keeps its own Esc = quit.
        enum class GameState { Title, Playing, Paused, Won };
        GameState gameState = GameState::Title;
        std::string hudHint; // per-room objective line, set by scene.lua

        // Week 10 (ship): user settings + run progress survive restarts.
        GameSettings settings = loadSettings(kSettingsPath);
        audio.setMasterVolume(settings.masterVolume);
        const auto cursorForState = [&]() {
            window.setCursorCaptured(playMode && gameState == GameState::Playing);
        };
        cursorForState(); // start on the title screen: cursor free

        // Scripting: gameplay decisions move to Lua (ADR-018). Script-spawned
        // bodies become full scene entities — they show up in the hierarchy.
        forge::ScriptEngine script;
        script.bindPhysics(physics);
        script.bindInput(window.input());
        script.bindAudio(audio);
        script.bindFx(sparks);
        int scriptBoxCount = 0;
        script.onBoxSpawned = [&](forge::BodyId body, glm::vec3 halfExtents) {
            const auto e = scene.create();
            scene.emplace<Name>(e, Name{"Box " + std::to_string(++scriptBoxCount)});
            scene.emplace<TransformC>(e).scale = halfExtents * 2.0f;
            scene.emplace<MeshRendererC>(e, MeshRendererC{cubeMesh, crateTexture});
            scene.emplace<BodyC>(e, BodyC{body, true});
        };
        // (scene.lua is loaded below, after ALL bindings are installed.)

        // Gravity-glove pickup needs "which ENTITY is this body?".
        std::unordered_map<uint32_t, forge::ecs::Entity> bodyToEntity;
        scene.each<Name>([&](forge::ecs::Entity e, Name&) {
            if (auto* b = scene.tryGet<BodyC>(e)) {
                bodyToEntity[b->id.value] = e;
            }
        });
        // Grab fix: Lua-spawned entities (room crates!) must be glove-visible
        // too — registration now happens for every spawn path.
        spawnRegisterHook = [&bodyToEntity](uint32_t body, forge::ecs::Entity e) {
            bodyToEntity[body] = e;
        };
        const auto onSpawnedOld = script.onBoxSpawned;
        script.onBoxSpawned = [&, onSpawnedOld](forge::BodyId body, glm::vec3 he) {
            onSpawnedOld(body, he);
            scene.each<Name>([&](forge::ecs::Entity e, Name&) {
                if (auto* b = scene.tryGet<BodyC>(e); b != nullptr && b->id.value == body.value) {
                    bodyToEntity[body.value] = e;
                }
            });
        };

        // Week 2 scene hooks: Lua addresses entities by NAME (door logic
        // lives in scene.lua); the host resolves names and keeps colliders
        // synced. Linear lookup is fine at this entity count.
        const auto findByName = [&](const std::string& name) {
            forge::ecs::Entity found = forge::ecs::kNullEntity;
            scene.each<Name>([&](forge::ecs::Entity e, Name& n) {
                if (n.value == name) {
                    found = e;
                }
            });
            return found;
        };
        script.onSetEntityPosition = [&](const std::string& name, glm::vec3 p) {
            const auto e = findByName(name);
            if (!scene.alive(e)) {
                return;
            }
            scene.get<TransformC>(e).position = p;
            if (auto* b = scene.tryGet<BodyC>(e)) {
                physics.teleport(b->id, p);
            }
        };
        script.onGetEntityPosition = [&](const std::string& name) {
            const auto e = findByName(name);
            return scene.alive(e) ? scene.get<TransformC>(e).position : glm::vec3(0.0f);
        };
        script.onSetEntityScale = [&](const std::string& name, glm::vec3 sc) {
            const auto e = findByName(name);
            if (scene.alive(e)) {
                scene.get<TransformC>(e).scale = sc;
            }
        };
        script.onDestroyEntity = [&](const std::string& name) {
            const auto e = findByName(name);
            if (!scene.alive(e)) {
                return;
            }
            if (auto* b = scene.tryGet<BodyC>(e)) {
                bodyToEntity.erase(b->id.value);
                physics.removeBody(b->id);
            }
            scene.destroy(e);
        };
        script.onSpawnEntity = [&](const std::string& name, glm::vec3 pos, glm::vec3 sc,
                                   glm::vec3 half, const std::string& texture, bool dynamic) {
            const auto it = textureByName.find(texture);
            spawnEntity(name, pos, sc, cubeMesh, it != textureByName.end() ? it->second : checker,
                        half, dynamic, 0.4f);
        };
        // Colour language (week 9): scripts retint by name — doors flip
        // red/green, plates orange/green. Unknown names are a no-op.
        script.onSetEntityTexture = [&](const std::string& name, const std::string& texture) {
            const auto e = findByName(name);
            const auto it = textureByName.find(texture);
            if (scene.alive(e) && it != textureByName.end()) {
                if (auto* mr = scene.tryGet<MeshRendererC>(e)) {
                    mr->texture = it->second;
                }
            }
        };
        script.onPlayerPosition = [&]() { return physics.characterPosition(); };
        // Per-room lighting pass (week 5): scene.lua tints the key light by
        // player z. Renderer holds it across frames; zero dir = keep the angle.
        script.onSetLighting = [&](glm::vec3 color, glm::vec3 dir) {
            renderer->setLighting(color, dir);
        };
        // Respawn/checkpoint (week 6): the script warps the player; the host
        // owns the character controller (zeroes velocity on the teleport).
        script.onSetPlayerPosition = [&](glm::vec3 p) {
            physics.setCharacterPosition(p);
            player.holding = false; // a warp drops whatever you were carrying
        };
        // Run complete (week 6): the script fires this once the finale clears.
        // The host raises the win screen and frees the cursor for the menu.
        script.onWin = [&]() {
            gameState = GameState::Won;
            cursorForState();
        };
        // Objective hint (week 7): the script owns the words, we own the pixels.
        script.onSetHint = [&](const std::string& text) { hudHint = text; };
        // Save seam (week 10): the script reports the furthest checkpoint;
        // we persist it. The title menu turns it into a Continue button.
        script.onSaveCheckpoint = [&](int checkpoint) {
            if (checkpoint != settings.savedCheckpoint) {
                settings.savedCheckpoint = checkpoint;
                saveSettings(kSettingsPath, settings);
            }
        };
        script.bindScene();
        script.runFile("assets/scripts/scene.lua");

        // Editor state: selection, simulation control, orbit camera.
        forge::ecs::Entity selected = forge::ecs::kNullEntity;
        bool paused = false;
        bool stepOnce = false;
        float camYaw = 39.0f;
        float camPitch = 17.0f;
        float camDist = 7.0f;
        const glm::vec3 camTarget{0.0f, 0.3f, 0.0f};

        // Edit assets/textures/crate.png, assets/models/torus.obj, or
        // assets/scripts/scene.lua while this runs — changes land in ~0.5 s.
        FileWatcher crateWatch{"assets/textures/crate.png"};
        FileWatcher torusWatch{"assets/models/torus.obj"};
        FileWatcher scriptWatch{"assets/scripts/scene.lua"};

        auto lastTime = std::chrono::steady_clock::now();
        auto lastPoll = lastTime;
        auto& input = window.input();
        while (!window.shouldClose()) {
            window.pollEvents();
            // ---- Menu FSM input. In the editor (TAB), Esc keeps its old
            // meaning: quit. In play mode, Esc drives the menu: Title/Won ->
            // quit, Playing <-> Paused. Enter starts a run from Title/Won.
            if (input.wasKeyPressed(forge::Key::Escape)) {
                if (!playMode) {
                    window.requestClose();
                } else if (gameState == GameState::Playing) {
                    gameState = GameState::Paused;
                    cursorForState();
                } else if (gameState == GameState::Paused) {
                    gameState = GameState::Playing;
                    cursorForState();
                } else { // Title or Won
                    window.requestClose();
                }
            }
            if (playMode && input.wasKeyPressed(forge::Key::Enter) &&
                (gameState == GameState::Title || gameState == GameState::Won)) {
                if (gameState == GameState::Won) {
                    script.runString("resetGame()"); // soft restart the run
                }
                gameState = GameState::Playing;
                cursorForState();
            }
            // C on the title: continue from the saved checkpoint (week 10).
            if (playMode && gameState == GameState::Title && settings.savedCheckpoint > 1 &&
                input.wasKeyPressed(forge::Key::C)) {
                gameState = GameState::Playing;
                cursorForState();
                script.runString("resumeAt(" + std::to_string(settings.savedCheckpoint) + ")");
            }
            if (input.wasKeyPressed(forge::Key::Tab)) {
                playMode = !playMode;
                player.holding = false; // no stale grabs across mode switches
                cursorForState();       // editor frees the cursor; play recaptures
            }

            // Gameplay simulation runs only while actually playing. In the
            // editor, the old Pause checkbox still governs; the menu states
            // (Title/Paused/Won) freeze the world behind the overlay.
            const bool simRun = playMode ? (gameState == GameState::Playing && !paused) : !paused;

            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            forge::Camera camera{};
            bool aimGrabbable = false; // crosshair affordance: "this will grab"
            if (playMode) {
                // ---- First-person: mouse-look + WASD through the capsule.
                // Mouse-look only while actually playing — a frozen menu must
                // not swing the camera. The look vector is still computed each
                // frame so the frozen scene renders from the right pose.
                if (simRun) {
                    const float sens = settings.mouseSensitivity;
                    const float ySign = settings.invertY ? -1.0f : 1.0f;
                    player.yaw += static_cast<float>(input.mouseDeltaX()) * sens;
                    player.pitch = std::clamp(
                        player.pitch - static_cast<float>(input.mouseDeltaY()) * sens * ySign,
                        -89.0f, 89.0f);
                }
                const float py = glm::radians(player.yaw);
                const float pp = glm::radians(player.pitch);
                const glm::vec3 look{std::cos(pp) * std::sin(py), std::sin(pp),
                                     -std::cos(pp) * std::cos(py)};
                const glm::vec3 forward{std::sin(py), 0.0f, -std::cos(py)};
                const glm::vec3 right{std::cos(py), 0.0f, std::sin(py)};

                // Movement, jumping and footsteps only advance while playing;
                // a paused/title/won world is frozen behind its menu.
                if (simRun) {
                    glm::vec3 move{0.0f};
                    if (input.isKeyDown(forge::Key::W)) {
                        move += forward;
                    }
                    if (input.isKeyDown(forge::Key::S)) {
                        move -= forward;
                    }
                    if (input.isKeyDown(forge::Key::D)) {
                        move += right;
                    }
                    if (input.isKeyDown(forge::Key::A)) {
                        move -= right;
                    }
                    if (move != glm::vec3(0.0f)) {
                        move = glm::normalize(move);
                    }
                    const float speed = input.isKeyDown(forge::Key::LeftShift) ? 7.0f : 4.5f;
                    const bool jumpNow = input.wasKeyPressed(forge::Key::Space);
                    if (jumpNow && physics.characterGrounded()) {
                        audio.play("assets/sounds/jump.wav");
                    }
                    physics.moveCharacter(move * speed, jumpNow, dt);

                    // Landing + footsteps ride the grounded state's edges.
                    const bool grounded = physics.characterGrounded();
                    if (grounded && !player.wasGrounded) {
                        audio.play("assets/sounds/land.wav");
                    }
                    player.wasGrounded = grounded;
                    if (grounded && move != glm::vec3(0.0f)) {
                        player.stepTimer -= dt * (speed > 5.0f ? 1.5f : 1.0f);
                        if (player.stepTimer <= 0.0f) {
                            audio.play("assets/sounds/step.wav");
                            player.stepTimer = 0.38f;
                        }
                    } else {
                        player.stepTimer = 0.12f; // first step lands quickly
                    }
                }

                const glm::vec3 eye = physics.characterPosition() + glm::vec3(0.0f, 0.65f, 0.0f);
                camera.position = eye;
                camera.target = eye + look;

                // QoL (week 7): light up the crosshair when the glove WOULD
                // grab — same raycast + dynamic-body test the click uses.
                if (simRun && !player.holding) {
                    if (const auto hit = physics.raycast(eye, look, 3.5f)) {
                        const auto it = bodyToEntity.find(hit->body.value);
                        auto* bc =
                            it != bodyToEntity.end() ? scene.tryGet<BodyC>(it->second) : nullptr;
                        aimGrabbable = bc != nullptr && bc->dynamic;
                    }
                }

                // ---- Gravity glove: grab / carry / throw / drop.
                if (simRun && input.wasMousePressed(forge::MouseButton::Left)) {
                    if (player.holding) { // throw
                        physics.setLinearVelocity(player.held, look * 11.0f);
                        player.holding = false;
                        audio.play("assets/sounds/throw.wav");
                        sparks.burst(eye + look * 1.2f, 12);
                    } else if (const auto hit = physics.raycast(eye, look, 3.5f)) {
                        const auto it = bodyToEntity.find(hit->body.value);
                        auto* bc =
                            it != bodyToEntity.end() ? scene.tryGet<BodyC>(it->second) : nullptr;
                        if (bc != nullptr && bc->dynamic) {
                            player.held = hit->body;
                            player.holding = true;
                            audio.play("assets/sounds/grab.wav");
                            FORGE_INFO("glove: grabbed {}", scene.get<Name>(it->second).value);
                        } else {
                            FORGE_INFO("glove: hit body {} (not grabbable)", hit->body.value);
                        }
                    } else {
                        FORGE_INFO(
                            "glove: miss (eye {:.1f},{:.1f},{:.1f} look {:.2f},{:.2f},{:.2f})",
                            eye.x, eye.y, eye.z, look.x, look.y, look.z);
                    }
                }
                if (simRun && player.holding &&
                    input.wasMousePressed(forge::MouseButton::Right)) {
                    physics.setLinearVelocity(player.held, {0.0f, 0.0f, 0.0f}); // gentle drop
                    player.holding = false;
                    audio.play("assets/sounds/grab.wav"); // same blip, release direction
                }
                if (simRun && player.holding) {
                    // Velocity spring: mass-independent, fights gravity for
                    // free, and held objects PUSH against obstacles instead
                    // of teleporting through them — that is the whole trick.
                    const glm::vec3 target = eye + look * 1.9f;
                    const glm::vec3 at = glm::vec3(physics.bodyTransform(player.held)[3]);
                    glm::vec3 springVel = (target - at) * 12.0f;
                    const float springLen = glm::length(springVel);
                    if (springLen > 14.0f) {
                        springVel *= 14.0f / springLen;
                    }
                    physics.setLinearVelocity(player.held, springVel);
                }
            } else {
                // ---- Editor orbit camera (unchanged from P7).
                if (!(ui && ui->wantCaptureMouse())) {
                    if (input.isMouseDown(forge::MouseButton::Right)) {
                        camYaw += static_cast<float>(input.mouseDeltaX()) * 0.4f;
                        camPitch =
                            std::clamp(camPitch + static_cast<float>(input.mouseDeltaY()) * 0.4f,
                                       -85.0f, 85.0f);
                    }
                    camDist = std::clamp(
                        camDist * std::pow(0.92f, static_cast<float>(input.scrollDeltaY())), 2.0f,
                        30.0f);
                }
                const float yawR = glm::radians(camYaw);
                const float pitchR = glm::radians(camPitch);
                camera.position =
                    camTarget + camDist * glm::vec3(std::sin(yawR) * std::cos(pitchR),
                                                    std::sin(pitchR),
                                                    std::cos(yawR) * std::cos(pitchR));
                camera.target = camTarget;
            }

            if (ui) {
                ui->beginFrame();
                ImGuizmo::BeginFrame();

                if (playMode && gameState == GameState::Playing) {
                    // Minimal HUD: crosshair (filled while holding) + hints.
                    // Only the live game shows it; the menus own the screen.
                    const ImGuiIO& io = ImGui::GetIO();
                    ImDrawList* draw = ImGui::GetForegroundDrawList();
                    const ImVec2 center{io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f};
                    if (player.holding) {
                        draw->AddCircleFilled(center, 5.0f, IM_COL32(255, 220, 120, 230));
                    } else if (aimGrabbable) { // amber ring: the glove will grab
                        draw->AddCircle(center, 6.0f, IM_COL32(255, 220, 120, 240), 0, 2.5f);
                    } else {
                        draw->AddCircle(center, 5.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
                    }
                    // Per-room objective hint (scene.lua sets it), top-centred.
                    if (!hudHint.empty()) {
                        const ImVec2 sz = ImGui::CalcTextSize(hudHint.c_str());
                        const ImVec2 at{(io.DisplaySize.x - sz.x) * 0.5f, 34.0f};
                        draw->AddRectFilled({at.x - 8.0f, at.y - 4.0f},
                                            {at.x + sz.x + 8.0f, at.y + sz.y + 4.0f},
                                            IM_COL32(0, 0, 0, 90), 4.0f);
                        draw->AddText(at, IM_COL32(255, 255, 255, 205), hudHint.c_str());
                    }
                    draw->AddText({12.0f, io.DisplaySize.y - 26.0f}, IM_COL32(255, 255, 255, 160),
                                  "WASD move  SPACE jump  LMB grab/throw  RMB drop  "
                                  "Q drone  R respawn  E crates  ESC pause  TAB editor");
                }

                // ---- Menu overlays (week 6). A centred window per non-playing
                // state. Buttons mirror the keyboard shortcuts so mouse works
                // too. Restart soft-resets the run via the Lua resetGame().
                if (playMode && gameState != GameState::Playing) {
                    const ImGuiIO& io = ImGui::GetIO();
                    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                            ImGuiCond_Always, {0.5f, 0.5f});
                    ImGui::SetNextWindowSize({320.0f, 0.0f}, ImGuiCond_Always);
                    ImGui::Begin("##menu", nullptr,
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
                    const auto startRun = [&](bool reset) {
                        if (reset) {
                            script.runString("resetGame()");
                        }
                        gameState = GameState::Playing;
                        cursorForState();
                    };
                    if (gameState == GameState::Title) {
                        ImGui::TextUnformatted("VAULT");
                        ImGui::Separator();
                        ImGui::TextWrapped("A physics puzzle in ten rooms. Carry crates, "
                                           "weigh down plates, break the glass, reach the exit.");
                        ImGui::Spacing();
                        if (ImGui::Button("Play", {-1.0f, 0.0f})) {
                            startRun(false);
                        }
                        // Saved progress (week 10): resume the run where the
                        // last session's furthest checkpoint left off.
                        if (settings.savedCheckpoint > 1 &&
                            ImGui::Button("Continue", {-1.0f, 0.0f})) {
                            startRun(false);
                            script.runString("resumeAt(" +
                                             std::to_string(settings.savedCheckpoint) + ")");
                        }
                        if (ImGui::Button("Quit", {-1.0f, 0.0f})) {
                            window.requestClose();
                        }
                        ImGui::Spacing();
                        ImGui::TextDisabled(settings.savedCheckpoint > 1
                                                ? "ENTER play   C continue   ESC quit"
                                                : "ENTER play   ESC quit");
                    } else if (gameState == GameState::Paused) {
                        ImGui::TextUnformatted("Paused");
                        ImGui::Separator();
                        if (ImGui::Button("Resume", {-1.0f, 0.0f})) {
                            gameState = GameState::Playing;
                            cursorForState();
                        }
                        if (ImGui::Button("Restart", {-1.0f, 0.0f})) {
                            startRun(true);
                        }
                        if (ImGui::Button("Quit", {-1.0f, 0.0f})) {
                            window.requestClose();
                        }
                        // Settings (week 10): the shippable three. Resolution/
                        // quality/rebinding are scope-cut with reasons (ADR-026).
                        ImGui::Spacing();
                        ImGui::TextDisabled("Settings");
                        ImGui::Separator();
                        bool changed = false;
                        changed |= ImGui::SliderFloat("sensitivity", &settings.mouseSensitivity,
                                                      0.05f, 0.35f, "%.2f");
                        changed |= ImGui::Checkbox("invert Y", &settings.invertY);
                        if (ImGui::SliderFloat("volume", &settings.masterVolume, 0.0f, 1.0f,
                                               "%.2f")) {
                            audio.setMasterVolume(settings.masterVolume);
                            changed = true;
                        }
                        if (changed) {
                            saveSettings(kSettingsPath, settings);
                        }
                        ImGui::Spacing();
                        ImGui::TextDisabled("ESC resume");
                    } else { // Won
                        ImGui::TextUnformatted("VAULT CLEARED");
                        ImGui::Separator();
                        ImGui::TextWrapped("Every room solved. Thanks for playing.");
                        ImGui::Spacing();
                        if (ImGui::Button("Play again", {-1.0f, 0.0f})) {
                            startRun(true);
                        }
                        if (ImGui::Button("Quit", {-1.0f, 0.0f})) {
                            window.requestClose();
                        }
                        ImGui::Spacing();
                        ImGui::TextDisabled("ENTER play again   ESC quit");
                    }
                    ImGui::End();
                }

                if (!playMode) {
                    // ---- Scene panel: simulation control + hierarchy.
                    ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize({250.0f, 400.0f}, ImGuiCond_FirstUseEver);
                    ImGui::Begin("Scene");
                    ImGui::Text("%.1f fps (%.2f ms)", 1.0 / static_cast<double>(dt),
                                static_cast<double>(dt) * 1000.0);
                    ImGui::Checkbox("Pause", &paused);
                    ImGui::SameLine();
                    if (ImGui::Button("Step")) {
                        stepOnce = true;
                    }
                    ImGui::Separator();
                    scene.each<Name>([&](forge::ecs::Entity e, Name& name) {
                        ImGui::PushID(static_cast<int>(e.index));
                        if (ImGui::Selectable(name.value.c_str(), selected == e)) {
                            selected = e;
                        }
                        ImGui::PopID();
                    });
                    ImGui::Separator();
                    ImGui::TextDisabled("RMB drag: orbit / wheel: zoom");
                    ImGui::TextDisabled("E: box rain / SPACE: kick (Lua)");
                    ImGui::End();

                    // ---- Inspector panel: the selected entity's components.
                    ImGui::SetNextWindowPos({1280.0f - 292.0f, 12.0f}, ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize({280.0f, 300.0f}, ImGuiCond_FirstUseEver);
                    ImGui::Begin("Inspector");
                    if (!scene.alive(selected)) {
                        ImGui::TextDisabled("select an entity in Scene");
                    } else {
                        auto& name = scene.get<Name>(selected);
                        char nameBuf[64]{};
                        std::snprintf(nameBuf, sizeof(nameBuf), "%s", name.value.c_str());
                        if (ImGui::InputText("name", nameBuf, sizeof(nameBuf))) {
                            name.value = nameBuf;
                        }
                        auto& t = scene.get<TransformC>(selected);
                        auto* body = scene.tryGet<BodyC>(selected);
                        if (body != nullptr && body->dynamic) {
                            // Physics owns this transform: edit = teleport.
                            glm::vec3 p = glm::vec3(physics.bodyTransform(body->id)[3]);
                            if (ImGui::DragFloat3("position", &p.x, 0.05f)) {
                                physics.teleport(body->id, p);
                            }
                            ImGui::TextDisabled("rotation/scale: physics-driven");
                            if (ImGui::Button("Kick")) {
                                physics.addLinearVelocity(body->id, {0.6f, 6.0f, 0.4f});
                            }
                        } else {
                            const bool moved = ImGui::DragFloat3("position", &t.position.x, 0.05f);
                            ImGui::DragFloat3("rotation", &t.eulerDeg.x, 0.5f);
                            ImGui::DragFloat3("scale", &t.scale.x, 0.02f);
                            if (moved && body != nullptr) {
                                physics.teleport(body->id, t.position); // collider follows
                            }
                        }
                    }
                    ImGui::End();

                    // ---- Translate gizmo on the selection (P7.3). ImGuizmo
                    // flips Y itself (expects GL-style NDC), so it gets the
                    // UNFLIPPED projection — hand it ours and everything drags
                    // upside-down. Same fov/near/far the renderer uses.
                    if (scene.alive(selected)) {
                        const ImGuiIO& io = ImGui::GetIO();
                        ImGuizmo::SetOrthographic(false);
                        ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
                        const glm::mat4 view =
                            glm::lookAt(camera.position, camera.target, glm::vec3(0, 1, 0));
                        const glm::mat4 proj = glm::perspective(glm::radians(camera.fovYDegrees),
                                                                io.DisplaySize.x / io.DisplaySize.y,
                                                                camera.nearPlane, camera.farPlane);

                        auto& t = scene.get<TransformC>(selected);
                        auto* body = scene.tryGet<BodyC>(selected);
                        glm::mat4 model = (body != nullptr && body->dynamic)
                                              ? physics.bodyTransform(body->id) *
                                                    glm::scale(glm::mat4(1.0f), t.scale)
                                              : trs(t);
                        if (ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::TRANSLATE,
                                                 ImGuizmo::WORLD, &model[0][0])) {
                            const glm::vec3 newPosition{model[3]};
                            t.position = newPosition;
                            if (body != nullptr) {
                                physics.teleport(body->id, newPosition);
                            }
                        }
                    }
                } // !playMode (editor panels + gizmo)
            }

            // Simulation control: simRun freezes gameplay+physics behind a
            // menu (or the editor's Pause). Step runs exactly one 60 Hz tick.
            // Typing in the UI suppresses gameplay input (the script polls keys).
            const bool typing = ui && ui->wantCaptureKeyboard();
            if (simRun) {
                if (!typing) {
                    script.update(dt); // gameplay decides first...
                }
                physics.update(dt); // ...then the world reacts
            } else if (stepOnce) {
                stepOnce = false;
                physics.update(1.0f / 60.0f);
            }
            audio.update(); // reap finished one-shot voices (loops persist)

            // Keyframe animation drives the platform; the collider rides
            // along via teleport (a kinematic body is the proper follow-up,
            // noted in ADR-020). A frozen sim freezes animation too.
            if (simRun || stepOnce) {
                animTime += dt;
            }
            if (scene.alive(platformEntity)) {
                auto& pt = scene.get<TransformC>(platformEntity);
                pt.position = platformClip.samplePosition(animTime);
                if (auto* pb = scene.tryGet<BodyC>(platformEntity)) {
                    physics.teleport(pb->id, pt.position);
                }
            }
            sparks.update(simRun ? dt : 0.0f);

            if (renderer && now - lastPoll > std::chrono::milliseconds(500)) {
                lastPoll = now;
                crateWatch.poll([&](const std::string& path) {
                    const auto img = forge::assets::loadImage(path);
                    renderer->updateTexture(crateTexture, img.width, img.height, img.rgba);
                });
                torusWatch.poll([&](const std::string& path) {
                    const auto data = forge::assets::loadMesh(path);
                    renderer->updateMesh(torusMesh, data.vertices, data.indices);
                });
                scriptWatch.poll([&](const std::string& path) {
                    if (script.runFile(path)) {
                        FORGE_INFO("script hot-reloaded: {}", path);
                    }
                });
            }

            if (renderer) {
                // The frame's draw list, straight from the scene: physics
                // owns dynamic transforms, TransformC owns everything else.
                std::vector<forge::DrawItem> items;
                scene.each<TransformC, MeshRendererC>([&](forge::ecs::Entity e, TransformC& t,
                                                          MeshRendererC& mr) {
                    glm::mat4 model;
                    if (auto* body = scene.tryGet<BodyC>(e); body != nullptr && body->dynamic) {
                        model =
                            physics.bodyTransform(body->id) * glm::scale(glm::mat4(1.0f), t.scale);
                    } else {
                        model = trs(t);
                    }
                    items.push_back({mr.mesh, mr.texture, model});
                });
                sparks.appendDrawItems(items, cubeMesh, crateTexture);
                renderer->drawFrame(camera, items);
            }
        }
    } catch (const std::exception& e) {
        FORGE_ERROR("fatal: {}", e.what());
        return 1;
    }

    FORGE_INFO("clean shutdown");
    return 0;
}
