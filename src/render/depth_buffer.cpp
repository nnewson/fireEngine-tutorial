#include <fire_engine/render/detail/depth_buffer.hpp>

#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>

#include <array>
#include <stdexcept>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */

/**
 * @brief Selects the first supported depth-only attachment format.
 * @param physicalDevice Device whose optimal-tiling format support is queried.
 * @return Supported format suitable for depth attachment use.
 * @throws std::runtime_error if neither tutorial depth format is supported.
 */
[[nodiscard]] vk::Format chooseDepthFormat(const vk::raii::PhysicalDevice& physicalDevice)
{
    constexpr std::array candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD16Unorm,
    };
    for (const vk::Format candidate : candidates)
    {
        const vk::FormatProperties properties = physicalDevice.getFormatProperties(candidate);
        if ((properties.optimalTilingFeatures &
             vk::FormatFeatureFlagBits::eDepthStencilAttachment) != vk::FormatFeatureFlags{})
        {
            return candidate;
        }
    }
    throw std::runtime_error("The selected device supports no depth-only attachment format");
}
/** @endcond */
} // namespace

/* --- Public member functions --- */

DepthBuffer::DepthBuffer(const Device& device, const MemoryAllocator& allocator,
                         vk::Extent2D extent)
    : format_{chooseDepthFormat(device.physicalDevice())},
      image_{allocator, extent.width, extent.height, format_,
             vk::ImageUsageFlagBits::eDepthStencilAttachment},
      view_{device.logicalDevice(), vk::ImageViewCreateInfo{
                                        .image = image_.handle(),
                                        .viewType = vk::ImageViewType::e2D,
                                        .format = format_,
                                        .subresourceRange = kDepthSubresourceRange,
                                    }}
{
}

vk::Format DepthBuffer::format() const noexcept
{
    return format_;
}

vk::Image DepthBuffer::image() const noexcept
{
    return image_.handle();
}

const vk::raii::ImageView& DepthBuffer::view() const noexcept
{
    return view_;
}
} // namespace fire_engine::detail
