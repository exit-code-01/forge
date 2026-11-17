// engine/src/renderer/RendererInternal.hpp  (PRIVATE — engine-internal seam)
//
// The narrow doorway between the Renderer and engine subsystems that must
// draw inside its frame (EditorUi). Lives in src/, so consumers outside the
// engine physically cannot include it (ADR-002's include-path enforcement).
#pragma once

#include "VulkanCommon.hpp"

#include <cstdint>

namespace forge {
class Renderer;
}

namespace forge::internal {

// Implemented by EditorUi: record UI draw commands into the renderer's final
// (composite) pass — color = the swapchain image, NO depth attachment — after
// the tone-mapped scene has been written, while dynamic rendering is active.
// UI colors therefore land on screen exactly as authored (never tone-mapped).
struct UiRenderHook {
    virtual ~UiRenderHook() = default;
    virtual void record(VkCommandBuffer cmd) = 0;
};

// Everything ImGui's Vulkan backend needs at init time.
struct RendererVkInfo {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED; // swapchain format
    // Formats of the pass the UI hook records into (the composite pass).
    // depthFormat is VK_FORMAT_UNDEFINED: that pass has no depth attachment.
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    uint32_t imageCount = 0;
};

[[nodiscard]] RendererVkInfo queryVkInfo(const Renderer& renderer);

} // namespace forge::internal
