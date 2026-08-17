#include <fire_engine/render/detail/swapchain_selection.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace fire_engine::detail
{
/** @cond INTERNAL */
namespace
{
/* --- File-local function declarations --- */

/**
 * @brief Returns whether a format preserves four-channel 8-bit sRGB output.
 * @param format Vulkan color format to classify.
 * @return True when the format is interchangeable for the tutorial's swapchain output.
 */
[[nodiscard]] constexpr bool isSrgbFormat(vk::Format format) noexcept;
} // namespace

/* --- Internal functions --- */

vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats)
{
    const auto preferred =
        std::ranges::find_if(formats,
                             [](const vk::SurfaceFormatKHR& candidate)
                             {
                                 return isSrgbFormat(candidate.format) &&
                                        candidate.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
                             });
    return preferred != formats.end() ? *preferred : formats.front();
}

vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& presentModes)
{
    return std::ranges::contains(presentModes, vk::PresentModeKHR::eMailbox)
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                          const vk::Extent2D framebufferExtent)
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        throw std::runtime_error("The framebuffer has zero area");
    }
    return {
        .width = std::clamp(framebufferExtent.width, capabilities.minImageExtent.width,
                            capabilities.maxImageExtent.width),
        .height = std::clamp(framebufferExtent.height, capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height),
    };
}

std::uint32_t chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    const std::uint32_t desiredCount = capabilities.minImageCount + 1;
    return capabilities.maxImageCount > 0 ? std::min(desiredCount, capabilities.maxImageCount)
                                          : desiredCount;
}

vk::CompositeAlphaFlagBitsKHR chooseCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    constexpr std::array kPreferredModes = {
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
        vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
        vk::CompositeAlphaFlagBitsKHR::eInherit,
    };
    for (const vk::CompositeAlphaFlagBitsKHR mode : kPreferredModes)
    {
        if (static_cast<bool>(capabilities.supportedCompositeAlpha & mode))
        {
            return mode;
        }
    }
    throw std::runtime_error("The presentation surface has no supported composite-alpha mode");
}

namespace
{
/* --- File-local functions --- */

constexpr bool isSrgbFormat(vk::Format format) noexcept
{
    switch (format)
    {
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Srgb:
    case vk::Format::eA8B8G8R8SrgbPack32:
        return true;
    default:
        return false;
    }
}
} // namespace
/** @endcond */
} // namespace fire_engine::detail
