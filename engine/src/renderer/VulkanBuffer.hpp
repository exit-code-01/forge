// engine/src/renderer/VulkanBuffer.hpp  (PRIVATE — never leaves src/)
//
// One VkBuffer + its VkDeviceMemory, RAII. Deliberately NOT a general
// allocator: P3 needs exactly two shapes — device-local buffers filled once
// through staging, and persistently-mapped uniform buffers. If allocation
// counts ever matter (P5 asset streaming), the decision to adopt VMA gets
// its own ADR; today it would be premature machinery.
#pragma once

#include "VulkanCommon.hpp"

#include <cstdint>

namespace forge {

class VulkanDevice;

class GpuBuffer {
public:
    GpuBuffer() = default;
    // HOST_VISIBLE buffers are mapped for their whole lifetime (mapped()).
    GpuBuffer(const VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage,
              VkMemoryPropertyFlags memoryProps);
    ~GpuBuffer();

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;

    [[nodiscard]] VkBuffer handle() const { return m_buffer; }
    [[nodiscard]] VkDeviceSize size() const { return m_size; }
    // Non-null only for HOST_VISIBLE buffers.
    [[nodiscard]] void* mapped() const { return m_mapped; }

    void writeBytes(const void* src, size_t byteCount); // HOST_VISIBLE only

private:
    void destroy();

    VkDevice m_device = VK_NULL_HANDLE; // borrowed
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    void* m_mapped = nullptr;
};

// One-shot command buffer pair for load-time GPU work (uploads, mip blits).
// endOneShot submits, BLOCKS until idle, and frees — load-time only, by
// contract.
[[nodiscard]] VkCommandBuffer beginOneShot(const VulkanDevice& device, VkCommandPool pool);
void endOneShot(const VulkanDevice& device, VkCommandPool pool, VkCommandBuffer cmd);

// Blocking one-shot copy into a device-local buffer via a throwaway staging
// buffer. Fine at LOAD time (mesh upload); never call this per frame.
void uploadToBuffer(const VulkanDevice& device, VkCommandPool pool, VkBuffer dst, const void* data,
                    VkDeviceSize byteCount);

} // namespace forge
