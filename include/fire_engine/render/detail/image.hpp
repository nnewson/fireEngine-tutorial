#pragma once

#include <cstdint>

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
 * @brief Owns one Vulkan image and the VMA allocation bound to it.
 *
 * The image uses device-preferred memory and is intended for GPU-only texture
 * access after data has been copied from a staging buffer. The allocator is
 * borrowed and must outlive this object.
 */
class AllocatedImage final
{
public:
    /**
     * @brief Creates a two-dimensional image and its bound allocation.
     * @param allocator VMA owner that must outlive this image.
     * @param width Image width in pixels; must be greater than zero.
     * @param height Image height in pixels; must be greater than zero.
     * @param format Pixel format used by the image.
     * @param usage Vulkan operations for which the image will be used.
     * @throws std::invalid_argument if either extent dimension is zero.
     * @throws std::runtime_error if VMA cannot create the image.
     */
    AllocatedImage(const MemoryAllocator& allocator, std::uint32_t width, std::uint32_t height,
                   vk::Format format, vk::ImageUsageFlags usage);

    /** @brief Destroys the image and frees its allocation together through VMA. */
    ~AllocatedImage();

    /// @brief Copy construction is disabled because the allocation has one owner.
    AllocatedImage(const AllocatedImage&) = delete;
    /// @brief Copy assignment is disabled because the allocation has one owner.
    AllocatedImage& operator=(const AllocatedImage&) = delete;
    /// @brief Move construction is disabled to keep allocator lifetime ordering explicit.
    AllocatedImage(AllocatedImage&&) = delete;
    /// @brief Move assignment is disabled to keep allocator lifetime ordering explicit.
    AllocatedImage& operator=(AllocatedImage&&) = delete;

    /** @brief Returns the Vulkan image. @return Non-owning handle valid for this lifetime. */
    [[nodiscard]] vk::Image handle() const noexcept;

private:
    VmaAllocator_T* allocator_ = nullptr;   ///< Borrowed allocator used for cleanup.
    VmaAllocation_T* allocation_ = nullptr; ///< VMA allocation bound to image_.
    vk::Image image_{};                     ///< Vulkan image destroyed through VMA.
};
/** @endcond */
} // namespace fire_engine::detail
