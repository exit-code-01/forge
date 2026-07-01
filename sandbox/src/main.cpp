// sandbox/src/main.cpp
//
// The sandbox is a CONSUMER of the engine — public headers only.
// Note what's absent: no GLFW include, no raw pointers, no manual cleanup.
// The Window destructor tears everything down when main() returns.

#include "forge/forge.hpp"
#include "forge/platform/Window.hpp"

#include <exception>

int main() {
    const auto v = forge::version();
    FORGE_INFO("Forge Engine v{}.{}.{}", v.major, v.minor, v.patch);

    try {
        forge::Window window({.title = "Forge Sandbox", .width = 1280, .height = 720});

        // The main loop. Everything the engine ever does happens in here.
        while (!window.shouldClose()) {
            window.pollEvents();
            if (window.isKeyDown(forge::Key::Escape)) {
                window.requestClose();
            }
            // P2 inserts here: renderer.beginFrame() / draw / present
        }
    } catch (const std::exception& e) {
        FORGE_ERROR("fatal: {}", e.what());
        return 1;
    }

    FORGE_INFO("clean shutdown");
    return 0;
}
