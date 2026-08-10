#pragma once

#include <cstdint>
#include <vector>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Vulkan-free decoded image stored as tightly packed four-channel RGBA8 pixels. */
struct ImageData
{
    std::uint32_t width = 0;          ///< Pixel width.
    std::uint32_t height = 0;         ///< Pixel height.
    std::vector<std::uint8_t> pixels; ///< Row-major red, green, blue, and alpha bytes.
};
} // namespace fire_engine
