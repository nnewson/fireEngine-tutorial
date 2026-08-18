#pragma once

#include <vulkan/vulkan.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */

/** @brief The sole color mip and array layer used by tutorial images. */
inline constexpr vk::ImageSubresourceRange kColorSubresourceRange{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};

/** @brief The sole depth mip and array layer used by the presentation attachment. */
inline constexpr vk::ImageSubresourceRange kDepthSubresourceRange{
    .aspectMask = vk::ImageAspectFlagBits::eDepth,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};
/** @endcond */
} // namespace fire_engine::detail
