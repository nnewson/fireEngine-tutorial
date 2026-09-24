#pragma once

#include <cstddef>

#include <vulkan/vulkan_raii.hpp>

#include <fire_engine/render/detail/image.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;
class MemoryAllocator;

/* --- Constants --- */

/** @brief Fixed directional-shadow resolution used by the tutorial. */
inline constexpr vk::Extent2D kShadowMapExtent{.width = 1024, .height = 1024};

/** @brief Exact image uses required by every shadow map. */
inline constexpr vk::ImageUsageFlags kShadowMapImageUsage =
    vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;

/* --- Classes --- */

class ShadowMap;

/** @brief Counts successful shadow-map constructions for one renderer lifetime. */
class ShadowMapCreationTracker final
{
public:
    /** @brief Returns the cumulative successful construction count. @return Map count. */
    [[nodiscard]] std::size_t count() const noexcept;

private:
    friend class ShadowMap;

    /** @brief Records one fully constructed image and view. */
    void recordCreation() noexcept;

    std::size_t count_ = 0; ///< Successful constructions for one renderer.
};

/** @brief Owns one fixed-size frame-slot-local sampled depth attachment. */
class ShadowMap final
{
public:
    /**
     * @brief Creates one sampled depth attachment and records its successful construction.
     * @param device Logical device used to create the depth-only image view.
     * @param allocator VMA owner used for the device-local image allocation.
     * @param format Format selected once for every map owned by this renderer.
     * @param creationTracker Renderer-local counter updated after image and view creation.
     */
    ShadowMap(const Device& device, const MemoryAllocator& allocator, vk::Format format,
              ShadowMapCreationTracker& creationTracker);

    /** @brief Releases the image view before its VMA-owned image. */
    ~ShadowMap() = default;

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&&) = delete;
    ShadowMap& operator=(ShadowMap&&) = delete;

    /** @brief Returns the selected sampled depth format. @return Shadow-map format. */
    [[nodiscard]] vk::Format format() const noexcept;
    /** @brief Returns the allocated image. @return Non-owning shadow-map image handle. */
    [[nodiscard]] vk::Image image() const noexcept;
    /** @brief Returns the depth-only image view. @return Owned shadow-map view. */
    [[nodiscard]] const vk::raii::ImageView& view() const noexcept;

private:
    vk::Format format_;        ///< Sampled depth-attachment format selected by the renderer.
    AllocatedImage image_;     ///< Fixed-size device-local image and VMA allocation.
    vk::raii::ImageView view_; ///< View destroyed before image.
};
/** @endcond */
} // namespace fire_engine::detail
