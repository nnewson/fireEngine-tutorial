#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <fire_engine/render/detail/image.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;
class MemoryAllocator;

/* --- Classes --- */

/** @brief Owns one frame-slot-local depth attachment for a swapchain extent. */
class DepthBuffer final
{
public:
    /**
     * @brief Selects a supported format and creates one depth image and view.
     * @param device Physical and logical device used for selection and view creation.
     * @param allocator VMA owner used for the image allocation.
     * @param extent Swapchain extent shared by the depth attachment.
     * @throws std::runtime_error if no supported depth-only format is available.
     */
    DepthBuffer(const Device& device, const MemoryAllocator& allocator, vk::Extent2D extent);

    /** @brief Releases the image view before its VMA-owned image. */
    ~DepthBuffer() = default;

    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;
    DepthBuffer(DepthBuffer&&) = delete;
    DepthBuffer& operator=(DepthBuffer&&) = delete;

    /** @brief Returns the selected depth format. @return Depth attachment format. */
    [[nodiscard]] vk::Format format() const noexcept;
    /** @brief Returns the allocated image. @return Non-owning depth image handle. */
    [[nodiscard]] vk::Image image() const noexcept;
    /** @brief Returns the depth-only image view. @return Owned depth view. */
    [[nodiscard]] const vk::raii::ImageView& view() const noexcept;

private:
    vk::Format format_;        ///< Supported depth-only attachment format.
    AllocatedImage image_;     ///< Extent-matched image and VMA allocation.
    vk::raii::ImageView view_; ///< View destroyed before image.
};
/** @endcond */
} // namespace fire_engine::detail
