// sandbox/src/main.cpp — P1.2 input demo
//
// Still zero GLFW here: keys and buttons arrive through forge:: types only.

#include "forge/forge.hpp"
#include "forge/platform/Window.hpp"

#include <exception>

int main() {
    const auto v = forge::version();
    FORGE_INFO("Forge Engine v{}.{}.{}", v.major, v.minor, v.patch);

    try {
        forge::Window window({.title = "Forge Sandbox", .width = 1280, .height = 720});
        auto& input = window.input();

        while (!window.shouldClose()) {
            window.pollEvents();

            if (input.wasKeyPressed(forge::Key::Escape)) {
                window.requestClose();
            }

            // Edge vs state, demonstrated: SPACE logs ONCE per tap even if
            // held; WASD logs continuously while down.
            if (input.wasKeyPressed(forge::Key::Space)) {
                FORGE_INFO("space tapped at cursor ({:.0f}, {:.0f})", input.mouseX(),
                           input.mouseY());
            }
            if (input.isKeyDown(forge::Key::W)) {
                FORGE_TRACE("W held");
            }

            if (input.wasMousePressed(forge::MouseButton::Left)) {
                FORGE_INFO("click at ({:.0f}, {:.0f})", input.mouseX(), input.mouseY());
            }
            if (input.scrollDeltaY() != 0.0) {
                FORGE_INFO("scroll {:+.1f}", input.scrollDeltaY());
            }
        }
    } catch (const std::exception& e) {
        FORGE_ERROR("fatal: {}", e.what());
        return 1;
    }

    FORGE_INFO("clean shutdown");
    return 0;
}
