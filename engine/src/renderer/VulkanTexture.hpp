// engine/src/renderer/VulkanTexture.hpp  (PRIVATE — never leaves src/)
//
// A sampled 2D texture: VkImage + memory + view + sampler, RAII. RGBA8 texels
// go in through a staging buffer, then the FULL mip chain is generated on the
// GPU with a vkCmdBlitImage cascade (each level filtered down from the one
// above). Trilinear sampling across those levels is what kills the shimmer
// on minified detail — the classic checkerboard-in-the-distance artifact.
//
// Load-time API (blocking one-shot submits), same contract as uploadToBuffer.
// P5's asset pipeline will feed this from files; the machinery won't change.
#pragma once

#include "VulkanCommon.hpp"

#include <cstdint>
#include <span>

namespace forge {

class VulkanDevice;

class VulkanTexture {
public:
    // rgba: tightly packed 8-bit RGBA, sRGB-authored (format is _SRGB, so the
    // hardware linearizes on sample). Throws on any Vulkan failure.
    VulkanTexture(const VulkanDevice& device, VkCommandPool pool, uint32_t width, uint32_t height,
                  std::span<const uint8_t> rgba);
    ~VulkanTexture();

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&&) = delete;
    VulkanTexture& operator=(VulkanTexture&&) = delete;

    [[nodiscard]] VkImageView view() const { return m_view; }
    [[nodiscard]] VkSampler sampler() const { return m_sampler; }

private:
    VkDevice m_device = VK_NULL_HANDLE; // borrowed
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    uint32_t m_mipLevels = 1;
};

} // namespace forge
