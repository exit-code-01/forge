// engine/include/forge/platform/Window.hpp
//
// RAII ownership of exactly one OS window.
//   - ctor acquires (initializes GLFW on first window, refcounted)
//   - dtor releases (terminates GLFW when the last window dies)
//   - non-copyable, non-movable: the engine has one window until proven
//     otherwise, and movability buys nothing but pointer-stealing bugs today.
//
// GLFW is an IMPLEMENTATION DETAIL: only the forward declaration below leaks,
// consumers never include a GLFW header. This is the seam that would let us
// swap SDL2 without touching sandbox/editor code.
//
// THREADING: window creation and pollEvents() are main-thread only (GLFW
// requirement). The render thread must never call into this class.

#pragma once

#include "forge/platform/Input.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow; // opaque; defined by GLFW inside the engine only

namespace forge {

struct WindowDesc {
    std::string title = "Forge";
    uint32_t width = 1280;
    uint32_t height = 720;
};

class Window {
public:
    explicit Window(const WindowDesc& desc); // throws std::runtime_error on failure
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    void pollEvents(); // main thread only
    [[nodiscard]] bool shouldClose() const;
    void requestClose();

    // Input state for this window, valid between pollEvents() calls.
    [[nodiscard]] Input& input() { return m_input; }
    [[nodiscard]] const Input& input() const { return m_input; }

    [[nodiscard]] uint32_t width() const { return m_width; }
    [[nodiscard]] uint32_t height() const { return m_height; }

    // For the renderer (P2: vkCreateSurface needs it). Not for app code.
    [[nodiscard]] GLFWwindow* nativeHandle() const { return m_handle; }

    // Instance extensions Vulkan needs to present to THIS platform's windows
    // (surface + platform-surface). Instance method on purpose: it requires
    // GLFW to be initialized, which constructing a Window guarantees.
    // Throws std::runtime_error if the machine has no Vulkan loader at all.
    [[nodiscard]] std::vector<const char*> requiredVulkanExtensions() const;

private:
    GLFWwindow* m_handle = nullptr;
    Input m_input;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace forge
