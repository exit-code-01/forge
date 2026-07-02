// engine/src/renderer/VulkanSwapchain.cpp
#include "VulkanSwapchain.hpp"
#include "VulkanDevice.hpp"
#include "forge/core/Log.hpp"

#include <algorithm>
#include <stdexcept>

namespace forge {

namespace {

VkSurfaceFormatKHR chooseFormat(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data());
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f; // sRGB: the hardware applies gamma on write, correctly
        }
    }
    return formats.at(0); // spec guarantees at least one
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t width, uint32_t height) {
    // Most platforms pin currentExtent to the window size; the UINT32_MAX
    // sentinel means "you choose" (Wayland) — clamp our framebuffer size.
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    return {std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
            std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)};
}

} // namespace

VulkanSwapchain::VulkanSwapchain(const VulkanDevice& device, VkSurfaceKHR surface, uint32_t width,
                                 uint32_t height)
    : m_device(device.device()) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical(), surface, &caps);

    const VkSurfaceFormatKHR surfaceFormat = chooseFormat(device.physical(), surface);
    m_format = surfaceFormat.format;
    m_extent = chooseExtent(caps, width, height);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) { // 0 means "no maximum"
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = m_format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = m_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync; MAILBOX is a P3+ option
    createInfo.clipped = VK_TRUE;

    // Combined family -> exclusive (fast path, ADR-010). Separate families ->
    // concurrent, trading a little bandwidth for zero ownership transfers.
    const uint32_t families[] = {device.graphicsFamily(), device.presentFamily()};
    if (device.graphicsFamily() != device.presentFamily()) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = families;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSwapchainKHR failed");
    }

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
    m_images.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, m_images.data());

    m_views.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_views[i]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImageView failed for swapchain image");
        }
    }

    FORGE_INFO("swapchain: {}x{}, {} images, format {}, FIFO", m_extent.width, m_extent.height,
               actualCount, m_format == VK_FORMAT_B8G8R8A8_SRGB ? "B8G8R8A8_SRGB" : "fallback");
}

VulkanSwapchain::~VulkanSwapchain() {
    for (const VkImageView view : m_views) {
        vkDestroyImageView(m_device, view, nullptr);
    }
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    }
}

} // namespace forge
