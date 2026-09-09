#pragma once

#include <optional>

#include <vulkan/vulkan.hpp>

#include <fire_engine/graphics/detail/frame_capture.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */

/**
 * @brief Maps a swapchain format to the matching sRGB byte-conversion contract.
 * @param format Vulkan format selected for the swapchain images.
 * @return Supported capture format, or no value for linear or unsupported formats.
 */
[[nodiscard]] std::optional<CaptureFormat> captureFormatFor(vk::Format format) noexcept;

/** @endcond */
} // namespace fire_engine::detail
