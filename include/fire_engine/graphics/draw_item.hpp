#pragma once

#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/math/mat4.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Vulkan-free draw emitted by scene traversal for one frame. */
struct DrawItem
{
    RenderObjectId renderObject;   ///< Prepared mesh/material relationship to draw.
    Mat4 world = Mat4::identity(); ///< Current object-to-world transform.
};
} // namespace fire_engine
