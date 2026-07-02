// engine/src/renderer/VulkanDevice.hpp  (PRIVATE — never leaves src/)
//
// Physical-device choice + logical device + queues, one RAII object.
// Selection policy (ADR-010): a GPU is eligible only if it offers Vulkan 1.3,
// a graphics queue, present support for OUR surface, VK_KHR_swapchain, and
// the dynamicRendering + synchronization2 features. Among eligible GPUs,
// discrete beats integrated. Ineligible hardware is skipped with a log line,
// never silently.
#pragma once

#include "VulkanCommon.hpp"

#include <cstdint>

namespace forge {

class VulkanDevice {
public:
    VulkanDevice(VkInstance instance, VkSurfaceKHR surface); // throws if no eligible GPU
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

    [[nodiscard]] VkPhysicalDevice physical() const { return m_physical; }
    [[nodiscard]] VkDevice device() const { return m_device; }
    [[nodiscard]] VkQueue graphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] VkQueue presentQueue() const { return m_presentQueue; }
    [[nodiscard]] uint32_t graphicsFamily() const { return m_graphicsFamily; }
    [[nodiscard]] uint32_t presentFamily() const { return m_presentFamily; }

    void waitIdle() const;

private:
    VkPhysicalDevice m_physical = VK_NULL_HANDLE; // owned by the instance, not us
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsFamily = 0;
    uint32_t m_presentFamily = 0;
};

} // namespace forge
