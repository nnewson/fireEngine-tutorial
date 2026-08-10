#pragma once

#include <optional>

#include <fire_engine/graphics/color4.hpp>
#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief CPU-side unlit material compiled by the tutorial renderer. */
struct Material
{
    Color4 baseColor{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f}; ///< Base-color factor.
    std::optional<TextureId> baseColorTexture; ///< Optional sampled base-color texture.
};
} // namespace fire_engine
