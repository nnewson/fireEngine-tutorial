#pragma once

#include <fire_engine/graphics/color4.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief CPU-side unlit material compiled by the tutorial renderer. */
struct Material
{
    Color4 baseColour{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f}; ///< Vertex colour factor.
};
} // namespace fire_engine
