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
#include <memory>
#include <string>
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
        }

        // Physics owns WHERE dynamic things are; the ECS owns WHAT exists.
        forge::PhysicsWorld physics;
        forge::ecs::Registry scene;

        // halfExtents == 0 means "no collider".
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
            }
            return e;
        };

        const auto checker = forge::Renderer::defaultTexture();
        spawnEntity("Ground", {0.0f, -0.85f, 0.0f}, {8.0f, 0.2f, 8.0f}, cubeMesh, checker,
                    {4.0f, 0.1f, 4.0f}, false);
        spawnEntity("Torus", {-1.6f, -0.5f, 0.3f}, {1.0f, 1.0f, 1.0f}, torusMesh, crateTexture,
                    {0.85f, 0.25f, 0.85f}, false);
        const auto cubeEntity = spawnEntity("Cube", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                                            cubeMesh, checker, {0.5f, 0.5f, 0.5f}, true, 0.4f);

        // P8 (lean): audio + scripted spark bursts + a keyframe-animated
        // platform. Skinned meshes are deferred by decision (ADR-020).
        forge::Audio audio;
        forge::fx::ParticleEmitter sparks;

        const auto platformEntity =
            spawnEntity("Platform", {2.2f, -0.4f, -1.5f}, {1.6f, 0.2f, 1.6f}, cubeMesh,
                        crateTexture, {0.8f, 0.1f, 0.8f}, false);
        forge::anim::Clip platformClip;
        platformClip.positionKeys = {
            {0.0f, {2.2f, -0.4f, -1.5f}},
            {2.5f, {2.2f, 1.6f, -1.5f}},
            {5.0f, {2.2f, -0.4f, -1.5f}},
        };
        float animTime = 0.0f;

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
        script.setGlobal("cube", scene.get<BodyC>(cubeEntity).id.value);
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
            if (input.wasKeyPressed(forge::Key::Escape)) {
                window.requestClose();
            }
            // The SPACE kick lives in scene.lua now — see onUpdate there.

            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            // Orbit camera — only when the UI isn't using the mouse. Uses
            // LAST frame's capture state before beginFrame; imperceptible.
            if (!(ui && ui->wantCaptureMouse())) {
                if (input.isMouseDown(forge::MouseButton::Right)) {
                    camYaw += static_cast<float>(input.mouseDeltaX()) * 0.4f;
                    camPitch = std::clamp(camPitch + static_cast<float>(input.mouseDeltaY()) * 0.4f,
                                          -85.0f, 85.0f);
                }
                camDist =
                    std::clamp(camDist * std::pow(0.92f, static_cast<float>(input.scrollDeltaY())),
                               2.0f, 30.0f);
            }
            const float yawR = glm::radians(camYaw);
            const float pitchR = glm::radians(camPitch);
            const forge::Camera camera{
                .position = camTarget + camDist * glm::vec3(std::sin(yawR) * std::cos(pitchR),
                                                            std::sin(pitchR),
                                                            std::cos(yawR) * std::cos(pitchR)),
                .target = camTarget};

            if (ui) {
                ui->beginFrame();
                ImGuizmo::BeginFrame();

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
                    glm::mat4 model =
                        (body != nullptr && body->dynamic)
                            ? physics.bodyTransform(body->id) * glm::scale(glm::mat4(1.0f), t.scale)
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
            }

            // Simulation control: pause freezes gameplay+physics; Step runs
            // exactly one 60 Hz tick. Typing in the UI suppresses gameplay
            // input (the script polls keys).
            const bool typing = ui && ui->wantCaptureKeyboard();
            if (!paused) {
                if (!typing) {
                    script.update(dt); // gameplay decides first...
                }
                physics.update(dt); // ...then the world reacts
            } else if (stepOnce) {
                stepOnce = false;
                physics.update(1.0f / 60.0f);
            }

            // Keyframe animation drives the platform; the collider rides
            // along via teleport (a kinematic body is the proper follow-up,
            // noted in ADR-020). Paused sim pauses animation too.
            if (!paused || stepOnce) {
                animTime += dt;
            }
            if (scene.alive(platformEntity)) {
                auto& pt = scene.get<TransformC>(platformEntity);
                pt.position = platformClip.samplePosition(animTime);
                if (auto* pb = scene.tryGet<BodyC>(platformEntity)) {
                    physics.teleport(pb->id, pt.position);
                }
            }
            sparks.update(paused ? 0.0f : dt);

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
