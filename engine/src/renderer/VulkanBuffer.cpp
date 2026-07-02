// engine/src/renderer/VulkanBuffer.cpp
#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace forge {

namespace {

// Vulkan advertises several memory heaps/types per GPU; each buffer tells us
// (via typeBits) which types it can live in, and we pick one that also has
// the properties WE need (device-local, host-visible, ...).
uint32_t findMemoryType(VkPhysicalDevice gpu, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        const bool allowed = (typeBits & (1u << i)) != 0;
        const bool suitable = (memProps.memoryTypes[i].propertyFlags & props) == props;
        if (allowed && suitable) {
            return i;
        }
    }
    throw std::runtime_error("no suitable Vulkan memory type");
}

} // namespace

GpuBuffer::GpuBuffer(const VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags memoryProps)
    : m_device(device.device()), m_size(size) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // graphics queue only
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer failed");
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, m_buffer, &requirements);

    // One vkAllocateMemory per buffer is fine at our object counts; drivers
    // cap total allocations (~4096), which is the future VMA trigger.
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(device.physical(), requirements.memoryTypeBits, memoryProps);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, m_buffer, nullptr);
        throw std::runtime_error("vkAllocateMemory failed");
    }
    vkBindBufferMemory(m_device, m_buffer, m_memory, 0);

    if ((memoryProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
        // Persistent map: mapping is not free, so do it once, not per write.
        vkMapMemory(m_device, m_memory, 0, VK_WHOLE_SIZE, 0, &m_mapped);
    }
}

GpuBuffer::~GpuBuffer() { destroy(); }

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : m_device(other.m_device), m_buffer(other.m_buffer), m_memory(other.m_memory),
      m_size(other.m_size), m_mapped(other.m_mapped) {
    other.m_buffer = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_mapped = nullptr;
    other.m_size = 0;
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        m_device = other.m_device;
        m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
        m_memory = std::exchange(other.m_memory, VK_NULL_HANDLE);
        m_mapped = std::exchange(other.m_mapped, nullptr);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

void GpuBuffer::destroy() {
    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_buffer, nullptr); // implicit unmap on free
        vkFreeMemory(m_device, m_memory, nullptr);
        m_buffer = VK_NULL_HANDLE;
        m_memory = VK_NULL_HANDLE;
        m_mapped = nullptr;
    }
}

void GpuBuffer::writeBytes(const void* src, size_t byteCount) {
    assert(m_mapped != nullptr && "writeBytes requires a HOST_VISIBLE buffer");
    assert(byteCount <= m_size);
    std::memcpy(m_mapped, src, byteCount);
    // HOST_COHERENT memory needs no explicit flush; all our mapped buffers
    // request it. If that ever changes, flush here — not at call sites.
}

void uploadToBuffer(const VulkanDevice& device, VkCommandPool pool, VkBuffer dst, const void* data,
                    VkDeviceSize byteCount) {
    GpuBuffer staging(device, byteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.writeBytes(data, byteCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device.device(), &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    const VkBufferCopy region{0, 0, byteCount};
    vkCmdCopyBuffer(cmd, staging.handle(), dst, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(device.graphicsQueue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(device.graphicsQueue()); // blocking — load-time only, by contract
    vkFreeCommandBuffers(device.device(), pool, 1, &cmd);
}

} // namespace forge
