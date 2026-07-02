// sandbox/src/main.cpp — P2 demo
// Order matters: ECS first (works on ANY machine, even headless CI), then the
// window (headless CI dies here, gracefully), then the renderer — which is
// OPTIONAL: no Vulkan means warn and keep running windowed.
// Window must precede VulkanContext: the surface extensions come from GLFW.

#include "forge/assets/Assets.hpp"
#include "forge/ecs/Registry.hpp"
#include "forge/forge.hpp"
#include "forge/physics/PhysicsWorld.hpp"
#include "forge/platform/Window.hpp"
#include "forge/renderer/Renderer.hpp"
#include "forge/renderer/VulkanContext.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <chrono>
#include <exception>
#include <memory>
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

} // namespace

int main() {
    const auto v = forge::version();
    FORGE_INFO("Forge Engine v{}.{}.{}", v.major, v.minor, v.patch);

    ecsDemo();
    physicsDemo();
    try {
        assetDemo();
    } catch (const std::exception& e) {
        FORGE_ERROR("fatal: {}", e.what());
        return 1; // missing/broken assets are a real failure, headless or not
    }

    try {
        forge::Window window({.title = "Forge Sandbox", .width = 1280, .height = 720});

        // Declaration order == destruction order: renderer dies before the
        // context, context before the window. Do not reorder.
        std::unique_ptr<forge::VulkanContext> vulkan;
        std::unique_ptr<forge::Renderer> renderer;
        try {
            vulkan = std::make_unique<forge::VulkanContext>(window.requiredVulkanExtensions());
            renderer = std::make_unique<forge::Renderer>(window, *vulkan);
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

        // Physics owns WHERE things are; rendering only asks. The mesh is a
        // unit cube, so each body's render matrix = bodyTransform * scale(size).
        forge::PhysicsWorld physics;
        physics.addBox({4.0f, 0.1f, 4.0f}, {0.0f, -0.85f, 0.0f}, forge::BodyType::Static);
        const auto cubeBody =
            physics.addBox({0.5f, 0.5f, 0.5f}, {0.0f, 3.0f, 0.0f}, forge::BodyType::Dynamic, 0.4f);
        const glm::mat4 groundOffset = glm::translate(glm::mat4(1.0f), {0.0f, -0.85f, 0.0f}) *
                                       glm::scale(glm::mat4(1.0f), {8.0f, 0.2f, 8.0f});

        // The imported torus: static display piece resting on the slab, with
        // a box collider so the kicked cube bounces off it.
        const glm::vec3 torusPos{-1.6f, -0.5f, 0.3f};
        physics.addBox({0.85f, 0.25f, 0.85f}, torusPos, forge::BodyType::Static);
        const glm::mat4 torusModel = glm::translate(glm::mat4(1.0f), torusPos);

        // Framed so the full drop (y=3 down to the slab) stays in shot.
        const forge::Camera camera{.position = {4.2f, 2.6f, 5.2f}, .target = {0.0f, 0.6f, 0.0f}};

        auto lastTime = std::chrono::steady_clock::now();
        auto& input = window.input();
        while (!window.shouldClose()) {
            window.pollEvents();
            if (input.wasKeyPressed(forge::Key::Escape)) {
                window.requestClose();
            }
            if (input.wasKeyPressed(forge::Key::Space)) {
                // Mass-independent kick: pop the cube up with a little spin-
                // inducing sideways bias. Wakes it if it fell asleep.
                physics.addLinearVelocity(cubeBody, {0.6f, 6.0f, 0.4f});
            }

            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;
            physics.update(dt);

            if (renderer) {
                const std::array items{
                    forge::DrawItem{cubeMesh, forge::Renderer::defaultTexture(),
                                    physics.bodyTransform(cubeBody)},
                    forge::DrawItem{torusMesh, crateTexture, torusModel},
                    forge::DrawItem{cubeMesh, forge::Renderer::defaultTexture(), groundOffset},
                };
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
