#pragma once

#include <cstdint>

#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine
{
/* --- Enums --- */

/** @brief Filtering modes retained by the initial non-mipmapped texture path. */
enum class TextureFilter : std::uint8_t
{
    eNearest, ///< Select the nearest texel.
    eLinear,  ///< Linearly blend neighboring texels.
};

/** @brief Coordinate-addressing modes required by the supported glTF subset. */
enum class TextureWrap : std::uint8_t
{
    eRepeat,         ///< Repeat at every integer boundary.
    eMirroredRepeat, ///< Repeat while mirroring alternate intervals.
    eClampToEdge,    ///< Extend the nearest edge texel.
};

/* --- POD structs --- */

/** @brief Vulkan-free relationship between decoded pixels and sampling behavior. */
struct Texture
{
    ImageId image;                                    ///< Decoded source pixels.
    TextureFilter minFilter = TextureFilter::eLinear; ///< Minification filter.
    TextureFilter magFilter = TextureFilter::eLinear; ///< Magnification filter.
    TextureWrap wrapU = TextureWrap::eRepeat;         ///< Horizontal addressing mode.
    TextureWrap wrapV = TextureWrap::eRepeat;         ///< Vertical addressing mode.
};
} // namespace fire_engine
