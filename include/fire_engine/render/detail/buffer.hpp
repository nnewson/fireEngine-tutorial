#pragma once

#include <cstddef>
#include <span>

#include <vulkan/vulkan.hpp>

/* --- External forward declarations --- */

struct VmaAllocation_T;
struct VmaAllocator_T;

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class MemoryAllocator;

/* --- Classes --- */

/**
 * @brief Owns one Vulkan buffer and the VMA allocation bound to it.
 *
 * The buffer uses host-visible memory. Mesh and frame data use it directly at
 * this tutorial scale, while texture upload uses it as the transfer source for
 * a device-local image.
 *
 * The allocator is borrowed and must outlive this object.
 */
class AllocatedBuffer final
{
public:
    /**
     * @brief Creates a host-writable buffer and its bound allocation.
     * @param allocator VMA owner that must outlive this buffer.
     * @param size Number of bytes in the buffer; must be greater than zero.
     * @param usage Vulkan operations for which the buffer will be used.
     * @throws std::invalid_argument if size is zero.
     * @throws std::runtime_error if VMA cannot create the buffer.
     */
    AllocatedBuffer(const MemoryAllocator& allocator, vk::DeviceSize size,
                    vk::BufferUsageFlags usage);

    /** @brief Destroys the buffer and frees its allocation together through VMA. */
    ~AllocatedBuffer();

    /// @brief Copy construction is disabled because the allocation has one owner.
    AllocatedBuffer(const AllocatedBuffer&) = delete;
    /// @brief Copy assignment is disabled because the allocation has one owner.
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;
    /// @brief Move construction is disabled to keep allocator lifetime ordering explicit.
    AllocatedBuffer(AllocatedBuffer&&) = delete;
    /// @brief Move assignment is disabled to keep allocator lifetime ordering explicit.
    AllocatedBuffer& operator=(AllocatedBuffer&&) = delete;

    /**
     * @brief Copies bytes from the CPU into this allocation and flushes them for the GPU.
     * @param bytes Source bytes copied synchronously before the function returns.
     * @param offset Destination offset measured from the start of the buffer.
     * @throws std::out_of_range if the requested range exceeds the buffer.
     * @throws std::runtime_error if VMA cannot map or flush the allocation.
     */
    void write(std::span<const std::byte> bytes, vk::DeviceSize offset = 0) const;

    /**
     * @brief Returns the Vulkan buffer bound to this allocation.
     * @return Non-owning handle valid for this object's lifetime.
     */
    [[nodiscard]] vk::Buffer handle() const noexcept;

    /**
     * @brief Returns the buffer's requested size.
     * @return Number of addressable bytes in this buffer.
     */
    [[nodiscard]] vk::DeviceSize size() const noexcept;

private:
    VmaAllocator_T* allocator_ = nullptr;   ///< Borrowed allocator used for writes and cleanup.
    VmaAllocation_T* allocation_ = nullptr; ///< VMA allocation bound to buffer_.
    vk::Buffer buffer_{};                   ///< Vulkan buffer destroyed through VMA.
    vk::DeviceSize size_ = 0;               ///< Bounds checked by write().
};
/** @endcond */
} // namespace fire_engine::detail
