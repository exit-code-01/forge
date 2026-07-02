// engine/src/renderer/VulkanSwapchain.hpp  (PRIVATE — never leaves src/)
//
// The ring of images we share with the OS compositor: acquire one, render
// into it, present it back. Policy (kept boring on purpose):
//   format:  B8G8R8A8_SRGB when offered (gamma-correct for free), else first
//   present: FIFO — vsync, the only mode Vulkan guarantees to exist
//   count:   driver minimum + 1, so acquire doesn't stall on the driver
// Resize/out-of-date is handled by DESTROYING and REBUILDING this object —
// the Renderer holds it by unique_ptr for exactly that reason.
#pragma once

#include "VulkanCommon.hpp"

#include <cstdint>
#include <vector>

namespace forge {

class VulkanDevice;

class VulkanSwapchain {
public:
    VulkanSwapchain(const VulkanDevice& device, VkSurfaceKHR surface, uint32_t width,
                    uint32_t height);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) = delete;
    VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

    [[nodiscard]] VkSwapchainKHR handle() const { return m_swapchain; }
    [[nodiscard]] VkFormat format() const { return m_format; }
    [[nodiscard]] VkExtent2D extent() const { return m_extent; }
    [[nodiscard]] uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }
    [[nodiscard]] VkImage image(uint32_t i) const { return m_images[i]; }
    [[nodiscard]] VkImageView view(uint32_t i) const { return m_views[i]; }

private:
    VkDevice m_device = VK_NULL_HANDLE; // borrowed, for the dtor
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images; // owned by the swapchain, not destroyed by us
    std::vector<VkImageView> m_views;
};

} // namespace forge
