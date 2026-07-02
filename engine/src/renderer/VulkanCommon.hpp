// engine/src/renderer/VulkanCommon.hpp  (PRIVATE — never leaves src/)
//
// The one way renderer internals include Vulkan. Vulkan's macros use C-style
// casts, so we relax -Wold-style-cast for renderer TUs at this third-party
// boundary — the same tradeoff VulkanContext.cpp already made.
#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <volk.h>

#include <cstdint>

namespace forge::vkutil {

// Sync2 image-layout transition — the one barrier shape the renderer needs.
// Mip-aware because mip generation transitions levels individually.
inline void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                            uint32_t baseMip, uint32_t mipCount, VkImageLayout from,
                            VkImageLayout to, VkPipelineStageFlags2 srcStage,
                            VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                            VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, baseMip, mipCount, 0, 1};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace forge::vkutil
