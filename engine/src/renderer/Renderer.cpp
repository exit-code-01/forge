// engine/src/renderer/Renderer.cpp
#include "VulkanCommon.hpp" // volk first: gives glfw3.h the Vulkan types below
#include "VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"

#include "forge/core/Log.hpp"
#include "forge/platform/Window.hpp"
#include "forge/renderer/Renderer.hpp"
#include "forge/renderer/VulkanContext.hpp"

// Second (and last) TU that includes GLFW, for exactly one call:
// glfwCreateWindowSurface. Documented as the ADR-004 exception in ADR-010.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace forge {

namespace {

// The REAL pixel size right now (HiDPI-safe, unlike the cached WindowDesc
// size). {0,0} while minimized — the caller must skip the frame.
VkExtent2D framebufferExtent(const Window& window) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window.nativeHandle(), &width, &height);
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

} // namespace

struct Renderer::Impl {
    Window& window;
    VulkanContext& context;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapchain> swapchain;

    Impl(Window& win, VulkanContext& ctx) : window(win), context(ctx) {}
};

Renderer::Renderer(Window& window, VulkanContext& context)
    : m_impl(std::make_unique<Impl>(window, context)) {
    // GLFW picks the right platform surface extension (win32/xlib/wayland/…)
    // so we don't ifdef per OS.
    if (glfwCreateWindowSurface(context.instance(), window.nativeHandle(), nullptr,
                                &m_impl->surface) != VK_SUCCESS) {
        throw std::runtime_error("glfwCreateWindowSurface failed");
    }
    m_impl->device = std::make_unique<VulkanDevice>(context.instance(), m_impl->surface);

    const VkExtent2D extent = framebufferExtent(window);
    m_impl->swapchain = std::make_unique<VulkanSwapchain>(*m_impl->device, m_impl->surface,
                                                          extent.width, extent.height);
}

Renderer::~Renderer() {
    if (m_impl->device) {
        m_impl->device->waitIdle(); // never destroy what the GPU still reads
    }
    m_impl->swapchain.reset(); // strict reverse order: swapchain, device, surface
    m_impl->device.reset();
    if (m_impl->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_impl->context.instance(), m_impl->surface, nullptr);
    }
}

void Renderer::drawFrame() {
    // P2.1: device only. Swapchain (P2.2) and the pipeline (P2.3) land next.
}

} // namespace forge
