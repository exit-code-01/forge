// engine/include/forge/renderer/Renderer.hpp
//
// Owns the full chain that puts pixels on a Window: surface -> device ->
// swapchain -> pipeline -> per-frame commands & sync. pimpl on purpose — not
// one Vulkan type reaches consumers, and one pointer hop per drawFrame() is
// free at frame granularity.
//
// Lifetimes (enforce by declaration order in the app): Window and
// VulkanContext MUST outlive the Renderer. drawFrame() is main-thread only
// for now — it queries the window's framebuffer size through GLFW.

#pragma once

#include <memory>

namespace forge {

class Window;
class VulkanContext;

class Renderer {
public:
    // Throws std::runtime_error if no eligible GPU / surface creation fails.
    Renderer(Window& window, VulkanContext& context);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Renders and presents one frame. Safe to call when minimized (no-op).
    void drawFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
