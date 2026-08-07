#pragma once

#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/math/mat4.hpp>

namespace fire_engine
{
/* --- POD structs --- */

#if defined(_MSC_VER)
// Mat4 deliberately carries 16-byte alignment for shader-compatible storage.
// DrawItem therefore requires padding around its smaller RenderObjectId member;
// C4324 reports that intentional layout rather than a correctness problem.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

/** @brief Vulkan-free draw emitted by scene traversal for one frame. */
struct DrawItem
{
    RenderObjectId renderObject;   ///< Prepared mesh/material relationship to draw.
    Mat4 world = Mat4::identity(); ///< Current object-to-world transform.
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace fire_engine
