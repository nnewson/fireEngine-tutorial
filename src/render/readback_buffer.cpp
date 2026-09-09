#include <fire_engine/render/detail/readback_buffer.hpp>

#include <fire_engine/render/detail/allocator.hpp>

#include <stdexcept>
#include <string>

#include <vk_mem_alloc.h>

namespace fire_engine::detail
{
/** @cond INTERNAL */

ReadbackBuffer::ReadbackBuffer(const MemoryAllocator& allocator, vk::DeviceSize size)
    : allocator_{allocator.handle()},
      size_{size}
{
    if (size == 0)
    {
        throw std::invalid_argument("A readback buffer cannot have zero size");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult result =
        vmaCreateBuffer(allocator_, &bufferInfo, &allocationInfo, &buffer, &allocation_, nullptr);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("VMA readback-buffer creation failed: " +
                                 vk::to_string(static_cast<vk::Result>(result)));
    }
    buffer_ = buffer;

    void* mapping = nullptr;
    result = vmaMapMemory(allocator_, allocation_, &mapping);
    if (result != VK_SUCCESS)
    {
        vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(buffer_), allocation_);
        buffer_ = nullptr;
        allocation_ = nullptr;
        throw std::runtime_error("VMA readback-buffer mapping failed: " +
                                 vk::to_string(static_cast<vk::Result>(result)));
    }
    mapped_ = static_cast<std::byte*>(mapping);
}

ReadbackBuffer::~ReadbackBuffer()
{
    vmaUnmapMemory(allocator_, allocation_);
    vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(buffer_), allocation_);
}

vk::Buffer ReadbackBuffer::handle() const noexcept
{
    return buffer_;
}

vk::DeviceSize ReadbackBuffer::size() const noexcept
{
    return size_;
}

std::span<const std::byte> ReadbackBuffer::bytes(std::size_t byteCount) const
{
    if (byteCount > size_)
    {
        throw std::out_of_range("Readback range exceeds the allocation");
    }
    const VkResult result = vmaInvalidateAllocation(allocator_, allocation_, 0, byteCount);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("VMA readback invalidation failed: " +
                                 vk::to_string(static_cast<vk::Result>(result)));
    }
    return {mapped_, byteCount};
}

/** @endcond */
} // namespace fire_engine::detail
