// engine/src/renderer/VulkanTexture.cpp
#include "VulkanTexture.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace forge {

VulkanTexture::VulkanTexture(const VulkanDevice& device, VkCommandPool pool, uint32_t width,
                             uint32_t height, std::span<const uint8_t> rgba)
    : m_device(device.device()) {
    if (rgba.size() != static_cast<size_t>(width) * height * 4) {
        throw std::runtime_error("texture data size does not match dimensions");
    }
    // floor(log2(maxDim)) + 1: every level halves until 1x1.
    m_mipLevels = std::bit_width(std::max(width, height));

    constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_SRGB;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kFormat;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = m_mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSFER_SRC because mip generation blits FROM each level.
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage (texture) failed");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, m_image, &requirements);
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device.physical(), &memProps);
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        const bool allowed = (requirements.memoryTypeBits & (1u << i)) != 0;
        const bool local =
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        if (allowed && local) {
            typeIndex = i;
            break;
        }
    }
    if (typeIndex == UINT32_MAX) {
        throw std::runtime_error("no device-local memory for texture");
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = typeIndex;
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (texture) failed");
    }
    vkBindImageMemory(m_device, m_image, m_memory, 0);

    // ---- Upload mip 0, then blit the chain down.
    GpuBuffer staging(device, rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.writeBytes(rgba.data(), rgba.size());

    const VkCommandBuffer cmd = beginOneShot(device, pool);

    vkutil::transitionImage(cmd, m_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipLevels,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging.handle(), m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copy);

    // Cascade: level i (TRANSFER_SRC) --blit--> level i+1 (TRANSFER_DST).
    int32_t mipW = static_cast<int32_t>(width);
    int32_t mipH = static_cast<int32_t>(height);
    for (uint32_t i = 1; i < m_mipLevels; ++i) {
        // The level we blit FROM was just written; make it readable.
        vkutil::transitionImage(cmd, m_image, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        const int32_t nextW = std::max(mipW / 2, 1);
        const int32_t nextH = std::max(mipH / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
        blit.srcOffsets[1] = {mipW, mipH, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        blit.dstOffsets[1] = {nextW, nextH, 1};
        vkCmdBlitImage(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        mipW = nextW;
        mipH = nextH;
    }

    // Everything to SHADER_READ_ONLY: levels 0..n-2 are TRANSFER_SRC by now,
    // the last is still TRANSFER_DST — transition the two groups separately.
    if (m_mipLevels > 1) {
        vkutil::transitionImage(
            cmd, m_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipLevels - 1,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    vkutil::transitionImage(
        cmd, m_image, VK_IMAGE_ASPECT_COLOR_BIT, m_mipLevels - 1, 1,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    endOneShot(device, pool, cmd);

    // ---- View + sampler.
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipLevels, 0, 1};
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_view) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView (texture) failed");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; // trilinear
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    // Anisotropy is a device FEATURE we haven't enabled; revisit with P5's
    // real assets where oblique floors make it visibly worth the request.
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler failed");
    }
}

VulkanTexture::~VulkanTexture() {
    vkDestroySampler(m_device, m_sampler, nullptr);
    vkDestroyImageView(m_device, m_view, nullptr);
    vkDestroyImage(m_device, m_image, nullptr);
    vkFreeMemory(m_device, m_memory, nullptr);
}

} // namespace forge
