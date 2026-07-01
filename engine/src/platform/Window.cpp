// engine/src/platform/Window.cpp
#include "forge/platform/Window.hpp"
#include "forge/core/Log.hpp"

// We're Vulkan-bound: stop glfw3.h from including system OpenGL headers.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace forge {

namespace {

// Refcount for GLFW's global init/terminate. Main-thread only by GLFW's own
// rules, so a plain int is fine — if this ever needs an atomic, the design
// is already broken.
int s_glfwRefCount = 0;

void glfwErrorCallback(int code, const char* description) {
    FORGE_ERROR("GLFW error {}: {}", code, description);
}

int toGlfwKey(Key key) {
    switch (key) {
    case Key::Escape:
        return GLFW_KEY_ESCAPE;
    }
    return GLFW_KEY_UNKNOWN;
}

} // namespace

Window::Window(const WindowDesc& desc) {
    if (s_glfwRefCount == 0) {
        glfwSetErrorCallback(glfwErrorCallback);
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("glfwInit failed — no display/backend available?");
        }
        FORGE_TRACE("GLFW initialized");
    }
    ++s_glfwRefCount;

    // Vulkan manages the surface itself; tell GLFW not to create a GL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_handle = glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height),
                                desc.title.c_str(), nullptr, nullptr);
    if (m_handle == nullptr) {
        if (--s_glfwRefCount == 0) {
            glfwTerminate();
        }
        throw std::runtime_error("glfwCreateWindow failed");
    }

    // Track the real framebuffer size — on HiDPI it differs from desc size,
    // and P2's swapchain must use THIS, not the requested size.
    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(m_handle, &fbWidth, &fbHeight);
    m_width = static_cast<uint32_t>(fbWidth);
    m_height = static_cast<uint32_t>(fbHeight);

    FORGE_INFO("window created: \"{}\" {}x{} (framebuffer {}x{})", desc.title, desc.width,
               desc.height, m_width, m_height);
}

Window::~Window() {
    glfwDestroyWindow(m_handle);
    if (--s_glfwRefCount == 0) {
        glfwTerminate();
        FORGE_TRACE("GLFW terminated");
    }
}

void Window::pollEvents() { glfwPollEvents(); }

bool Window::shouldClose() const { return glfwWindowShouldClose(m_handle) == GLFW_TRUE; }

void Window::requestClose() { glfwSetWindowShouldClose(m_handle, GLFW_TRUE); }

bool Window::isKeyDown(Key key) const { return glfwGetKey(m_handle, toGlfwKey(key)) == GLFW_PRESS; }

} // namespace forge
