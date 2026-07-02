// engine/include/forge/renderer/VulkanContext.hpp
//
// Owns the VkInstance — the root object of everything Vulkan. RAII, same
// pattern as Window: ctor acquires (loader -> instance -> GPU enumeration),
// dtor releases. Vulkan headers do NOT leak into consumers; the handle
// typedef below is byte-identical to Vulkan's own, which C++ permits.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

typedef struct VkInstance_T* VkInstance; // mirrors VK_DEFINE_HANDLE(VkInstance)

namespace forge {

struct GpuInfo {
    std::string name;
    uint32_t apiMajor = 0;
    uint32_t apiMinor = 0;
    bool discrete = false;
};

class VulkanContext {
public:
    // extensions: instance extensions required by the platform (P2.1 passes
    // glfwGetRequiredInstanceExtensions for surface support). Throws
    // std::runtime_error if no Vulkan loader/driver exists on this machine.
    explicit VulkanContext(std::vector<const char*> extensions = {});
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    [[nodiscard]] VkInstance instance() const { return m_instance; }
    [[nodiscard]] const std::vector<GpuInfo>& gpus() const { return m_gpus; }

private:
    VkInstance m_instance = nullptr;
    std::vector<GpuInfo> m_gpus;
    bool m_validationEnabled = false;
};

} // namespace forge
