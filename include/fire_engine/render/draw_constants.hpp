#pragma once

#include <cstddef>

#include <fire_engine/graphics/color4.hpp>
#include <fire_engine/math/mat4.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Per-draw values supplied through Vulkan push constants. */
struct alignas(16) DrawConstants
{
    Mat4 model = Mat4::identity(); ///< Object-to-world transform for one scene node.
    Color4 baseColour{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f}; ///< Material tint.
};

static_assert(sizeof(DrawConstants) == 20 * sizeof(float));
static_assert(alignof(DrawConstants) == 16);
static_assert(offsetof(DrawConstants, baseColour) == 16 * sizeof(float));
} // namespace fire_engine
