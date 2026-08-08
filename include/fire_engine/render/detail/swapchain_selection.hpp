#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/**
 * @brief Chooses an sRGB surface format when available.
 * @param formats Non-empty format and color-space pairs reported by the surface.
 * @return Preferred sRGB pair, or the first supported pair as a fallback.
 */
[[nodiscard]] vk::SurfaceFormatKHR
chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);

/**
 * @brief Chooses low-latency mailbox presentation when supported.
 * @param presentModes Presentation modes reported by the surface.
 * @return Mailbox when available, otherwise Vulkan's guaranteed FIFO mode.
 */
[[nodiscard]] vk::PresentModeKHR
choosePresentMode(const std::vector<vk::PresentModeKHR>& presentModes);

/**
 * @brief Chooses a swapchain extent compatible with the surface and framebuffer.
 * @param capabilities Surface extent limits and any platform-defined fixed extent.
 * @param framebufferExtent Current drawable size in physical pixels.
 * @return Fixed surface extent or clamped framebuffer extent.
 * @throws std::runtime_error if a variable surface receives a zero-area framebuffer.
 */
[[nodiscard]] vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                                        const vk::Extent2D framebufferExtent);

/**
 * @brief Requests one image beyond the surface minimum to reduce pipeline stalls.
 * @param capabilities Surface-supported image-count range.
 * @return Desired image count capped by a finite surface maximum.
 */
[[nodiscard]] std::uint32_t chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities);

/**
 * @brief Chooses the first conventional alpha-compositing mode supported by the surface.
 * @param capabilities Surface flags describing supported alpha behaviour.
 * @return A supported composite-alpha mode, preferring opaque output.
 * @throws std::runtime_error if the surface reports no recognized mode.
 */
[[nodiscard]] vk::CompositeAlphaFlagBitsKHR
chooseCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities);
/** @endcond */
} // namespace fire_engine::detail
