#include <fire_engine/render/detail/shadow_map.hpp>

#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

std::size_t ShadowMapCreationTracker::count() const noexcept
{
    return count_;
}

void ShadowMapCreationTracker::recordCreation() noexcept
{
    ++count_;
}

ShadowMap::ShadowMap(const Device& device, const MemoryAllocator& allocator, vk::Format format,
                     ShadowMapCreationTracker& creationTracker)
    : format_{format},
      image_{allocator, kShadowMapExtent.width, kShadowMapExtent.height, format_,
             kShadowMapImageUsage},
      view_{device.logicalDevice(), vk::ImageViewCreateInfo{
                                        .image = image_.handle(),
                                        .viewType = vk::ImageViewType::e2D,
                                        .format = format_,
                                        .subresourceRange = kDepthSubresourceRange,
                                    }}
{
    creationTracker.recordCreation();
}

vk::Format ShadowMap::format() const noexcept
{
    return format_;
}

vk::Image ShadowMap::image() const noexcept
{
    return image_.handle();
}

const vk::raii::ImageView& ShadowMap::view() const noexcept
{
    return view_;
}
/** @endcond */
} // namespace fire_engine::detail
