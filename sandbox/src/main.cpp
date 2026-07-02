// sandbox/src/main.cpp — P1 complete + P2.0 demo
// Order matters for the demo: ECS runs first (works on ANY machine, even
// headless CI), then Vulkan probe (optional — warns and continues without),
// then the window loop.

#include "forge/ecs/Registry.hpp"
#include "forge/forge.hpp"
#include "forge/platform/Window.hpp"
#include "forge/renderer/VulkanContext.hpp"

#include <glm/vec3.hpp>

#include <exception>
#include <memory>

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

} // namespace

int main() {
    const auto v = forge::version();
    FORGE_INFO("Forge Engine v{}.{}.{}", v.major, v.minor, v.patch);

    ecsDemo();

    // Renderer is OPTIONAL at this stage: no Vulkan = warn and keep going.
    std::unique_ptr<forge::VulkanContext> vulkan;
    try {
        vulkan = std::make_unique<forge::VulkanContext>();
    } catch (const std::exception& e) {
        FORGE_WARN("renderer unavailable: {}", e.what());
    }

    try {
        forge::Window window({.title = "Forge Sandbox", .width = 1280, .height = 720});
        auto& input = window.input();
        while (!window.shouldClose()) {
            window.pollEvents();
            if (input.wasKeyPressed(forge::Key::Escape)) {
                window.requestClose();
            }
            if (input.wasKeyPressed(forge::Key::Space)) {
                FORGE_INFO("space at ({:.0f}, {:.0f})", input.mouseX(), input.mouseY());
            }
        }
    } catch (const std::exception& e) {
        FORGE_ERROR("fatal: {}", e.what());
        return 1;
    }

    FORGE_INFO("clean shutdown");
    return 0;
}
