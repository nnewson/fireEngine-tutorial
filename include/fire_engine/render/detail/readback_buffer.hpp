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
 * @brief Owns one persistently mapped host-readable transfer destination.
 *
 * The allocator is borrowed and must outlive this object. GPU writes must have
 * completed before bytes() is called; bytes() performs the VMA invalidation
 * needed before the host reads non-coherent memory.
 */
class ReadbackBuffer final
{
public:
    /**
     * @brief Creates and maps one host-readable transfer-destination buffer.
     * @param allocator VMA owner that must outlive this buffer.
     * @param size Number of addressable bytes; must be greater than zero.
     * @throws std::invalid_argument if size is zero.
     * @throws std::runtime_error if VMA cannot create or map the allocation.
     */
    ReadbackBuffer(const MemoryAllocator& allocator, vk::DeviceSize size);

    /** @brief Unmaps the allocation before destroying the buffer and memory. */
    ~ReadbackBuffer();

    ReadbackBuffer(const ReadbackBuffer&) = delete;
    ReadbackBuffer& operator=(const ReadbackBuffer&) = delete;
    ReadbackBuffer(ReadbackBuffer&&) = delete;
    ReadbackBuffer& operator=(ReadbackBuffer&&) = delete;

    /** @brief Returns the Vulkan transfer destination. @return Borrowed buffer handle. */
    [[nodiscard]] vk::Buffer handle() const noexcept;

    /** @brief Returns the allocation size. @return Addressable bytes in the buffer. */
    [[nodiscard]] vk::DeviceSize size() const noexcept;

    /**
     * @brief Invalidates and exposes bytes written by a completed GPU submission.
     * @param byteCount Leading bytes required by the caller.
     * @return Read-only mapped range valid until this object is destroyed.
     * @throws std::out_of_range if byteCount exceeds the allocation.
     * @throws std::runtime_error if VMA cannot invalidate non-coherent memory.
     */
    [[nodiscard]] std::span<const std::byte> bytes(std::size_t byteCount) const;

private:
    VmaAllocator_T* allocator_ = nullptr;   ///< Borrowed VMA owner used for cleanup.
    VmaAllocation_T* allocation_ = nullptr; ///< Allocation bound to buffer_.
    vk::Buffer buffer_{};                   ///< Transfer destination destroyed through VMA.
    std::byte* mapped_ = nullptr;           ///< Persistent mapping owned by this allocation.
    vk::DeviceSize size_ = 0;               ///< Bounds checked by bytes().
};

/** @endcond */
} // namespace fire_engine::detail
