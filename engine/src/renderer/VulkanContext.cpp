// engine/src/renderer/VulkanContext.cpp
#include "forge/renderer/VulkanContext.hpp"
#include "forge/core/Log.hpp"

// Vulkan's own macros use C-style casts; silence OUR -Wold-style-cast for
// this third-party boundary file only.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <volk.h>

#include <cstring>
#include <stdexcept>

namespace forge {

namespace {

bool validationLayerAvailable() {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            return true;
        }
    }
    return false;
}

const char* deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "software";
    default:
        return "other";
    }
}

} // namespace

VulkanContext::VulkanContext(std::vector<const char*> extensions) {
    // Step 1: find the Vulkan loader (vulkan-1.dll / libvulkan.so) at runtime.
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Vulkan loader not found — is a GPU driver installed?");
    }
    const uint32_t loaderVersion = volkGetInstanceVersion();
    FORGE_INFO("Vulkan loader {}.{}", VK_API_VERSION_MAJOR(loaderVersion),
               VK_API_VERSION_MINOR(loaderVersion));

    // Step 2: create the instance — our connection to the Vulkan runtime.
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Forge Sandbox";
    appInfo.pEngineName = "Forge";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> layers;
#ifndef NDEBUG
    // Validation is the single highest-value Vulkan debugging tool. Ship
    // builds omit it; dev builds enable it whenever the SDK provides it.
    if (validationLayerAvailable()) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        m_validationEnabled = true;
        FORGE_INFO("validation layer: ON");
    } else {
        FORGE_WARN("validation layer unavailable (install the Vulkan SDK to get it)");
    }
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
        result != VK_SUCCESS) {
        if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
            throw std::runtime_error("no Vulkan-capable driver (loader present, no ICD)");
        }
        throw std::runtime_error("vkCreateInstance failed");
    }
    volkLoadInstance(m_instance); // load all instance-level function pointers

    // Step 3: see what hardware exists. Selection happens in P2.1.
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &gpuCount, nullptr);
    std::vector<VkPhysicalDevice> devices(gpuCount);
    vkEnumeratePhysicalDevices(m_instance, &gpuCount, devices.data());

    for (const VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        m_gpus.push_back({
            .name = props.deviceName,
            .apiMajor = VK_API_VERSION_MAJOR(props.apiVersion),
            .apiMinor = VK_API_VERSION_MINOR(props.apiVersion),
            .discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
        });
        FORGE_INFO("GPU: {} ({}), Vulkan {}.{}", props.deviceName, deviceTypeName(props.deviceType),
                   VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion));
    }
    if (m_gpus.empty()) {
        FORGE_WARN("Vulkan instance created but no GPUs enumerated");
    }
}

VulkanContext::~VulkanContext() {
    if (m_instance != nullptr) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

} // namespace forge
