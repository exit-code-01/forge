// engine/src/renderer/VulkanDevice.cpp
#include "VulkanDevice.hpp"
#include "forge/core/Log.hpp"

#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace forge {

namespace {

constexpr uint32_t kNoFamily = std::numeric_limits<uint32_t>::max();

struct QueueFamilies {
    uint32_t graphics = kNoFamily;
    uint32_t present = kNoFamily;
    [[nodiscard]] bool complete() const { return graphics != kNoFamily && present != kNoFamily; }
};

// Prefer ONE family that does both graphics and present (true on virtually
// all hardware): it lets the swapchain use exclusive sharing, no ownership
// transfers. Separate families are still accepted as a fallback.
QueueFamilies findQueueFamilies(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());

    QueueFamilies out;
    for (uint32_t i = 0; i < count; ++i) {
        const bool graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
        if (graphics && present == VK_TRUE) {
            return {.graphics = i, .present = i};
        }
        if (graphics && out.graphics == kNoFamily) {
            out.graphics = i;
        }
        if (present == VK_TRUE && out.present == kNoFamily) {
            out.present = i;
        }
    }
    return out;
}

bool hasSwapchainExtension(VkPhysicalDevice gpu) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data());
    for (const auto& ext : extensions) {
        if (std::strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    return false;
}

bool hasRequired13Features(VkPhysicalDevice gpu) {
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(gpu, &features2);
    return features13.dynamicRendering == VK_TRUE && features13.synchronization2 == VK_TRUE;
}

// Negative = ineligible (with the reason logged). Otherwise higher wins.
int scoreGpu(VkPhysicalDevice gpu, VkSurfaceKHR surface, const VkPhysicalDeviceProperties& props) {
    if (props.apiVersion < VK_API_VERSION_1_3) {
        FORGE_TRACE("skipping {}: Vulkan < 1.3", props.deviceName);
        return -1;
    }
    if (!findQueueFamilies(gpu, surface).complete()) {
        FORGE_TRACE("skipping {}: no graphics+present queues for our surface", props.deviceName);
        return -1;
    }
    if (!hasSwapchainExtension(gpu)) {
        FORGE_TRACE("skipping {}: no VK_KHR_swapchain", props.deviceName);
        return -1;
    }
    if (!hasRequired13Features(gpu)) {
        FORGE_TRACE("skipping {}: missing dynamicRendering/synchronization2", props.deviceName);
        return -1;
    }
    int score = 1;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000; // discrete beats integrated; everything else is a tiebreak
    }
    return score;
}

} // namespace

VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());

    int bestScore = -1;
    VkPhysicalDeviceProperties bestProps{};
    for (const VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);
        const int score = scoreGpu(gpu, surface, props);
        if (score > bestScore) {
            bestScore = score;
            m_physical = gpu;
            bestProps = props;
        }
    }
    if (bestScore < 0 || m_physical == VK_NULL_HANDLE) {
        throw std::runtime_error("no Vulkan 1.3 GPU with graphics+present+swapchain support");
    }

    const QueueFamilies families = findQueueFamilies(m_physical, surface);
    m_graphicsFamily = families.graphics;
    m_presentFamily = families.present;

    // One queue per UNIQUE family (usually just one total).
    const std::set<uint32_t> uniqueFamilies{m_graphicsFamily, m_presentFamily};
    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (const uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    // Enable exactly the 1.3 features we depend on — nothing speculative.
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features2; // features go through pNext in the 1.1+ style
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    if (vkCreateDevice(m_physical, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDevice failed");
    }
    // Single-device engine: load device-level entry points directly (skips
    // the loader's per-call dispatch trampoline).
    volkLoadDevice(m_device);

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);

    FORGE_INFO("selected GPU: {} (score {}), graphics family {}, present family {}{}",
               bestProps.deviceName, bestScore, m_graphicsFamily, m_presentFamily,
               m_graphicsFamily == m_presentFamily ? " (combined)" : "");
}

VulkanDevice::~VulkanDevice() {
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
}

void VulkanDevice::waitIdle() const { vkDeviceWaitIdle(m_device); }

} // namespace forge
