#include <fire_engine/render/detail/buffer.hpp>

#include <fire_engine/render/detail/allocator.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>

#include <vk_mem_alloc.h>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

AllocatedBuffer::AllocatedBuffer(const MemoryAllocator& allocator, vk::DeviceSize size,
                                 vk::BufferUsageFlags usage)
    : allocator_{allocator.handle()},
      size_{size}
{
    if (size == 0)
    {
        throw std::invalid_argument("A Vulkan buffer cannot have zero size");
    }

    // These C structures contain optional fields that remain zero here. Assign
    // the handful this buffer needs after value-initializing the whole object,
    // keeping that intentional omission warning-clean on every compiler.
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // VMA chooses a suitable host-visible memory type. Sequential-write access
    // is enough for immutable meshes, texture staging, and the small frame uniform.
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkBuffer buffer = VK_NULL_HANDLE;
    const VkResult result =
        vmaCreateBuffer(allocator_, &bufferInfo, &allocationInfo, &buffer, &allocation_, nullptr);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("VMA buffer creation failed: " +
                                 vk::to_string(static_cast<vk::Result>(result)));
    }
    buffer_ = buffer;
}

AllocatedBuffer::~AllocatedBuffer()
{
    vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(buffer_), allocation_);
}

void AllocatedBuffer::write(std::span<const std::byte> bytes, vk::DeviceSize offset) const
{
    const vk::DeviceSize byteCount = bytes.size();
    if (offset > size_ || byteCount > size_ - offset)
    {
        throw std::out_of_range("Buffer upload exceeds the allocation");
    }
    if (bytes.empty())
    {
        return;
    }

    // This convenience call temporarily maps the allocation and performs any
    // flush required for non-coherent host-visible memory.
    const VkResult result =
        vmaCopyMemoryToAllocation(allocator_, bytes.data(), allocation_, offset, byteCount);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("VMA buffer upload failed: " +
                                 vk::to_string(static_cast<vk::Result>(result)));
    }
}

vk::Buffer AllocatedBuffer::handle() const noexcept
{
    return buffer_;
}

vk::DeviceSize AllocatedBuffer::size() const noexcept
{
    return size_;
}
/** @endcond */
} // namespace fire_engine::detail
