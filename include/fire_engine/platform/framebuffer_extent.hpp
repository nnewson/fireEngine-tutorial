#pragma once

#include <cstdint>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Drawable framebuffer dimensions in physical pixels. */
struct FramebufferExtent
{
    std::uint32_t width = 0;  ///< Horizontal pixel count, or zero while not drawable.
    std::uint32_t height = 0; ///< Vertical pixel count, or zero while not drawable.
};
} // namespace fire_engine
