#pragma once

#include <fire_engine/math/mat4.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/**
 * @brief Per-frame values read by the tutorial vertex shader.
 *
 * Slang declares the matching constant buffer with Std140DataLayout. A 4x4
 * float matrix occupies 64 bytes and has 16-byte base alignment in that layout.
 */
struct alignas(16) FrameUniforms
{
    Mat4 viewProjection = Mat4::identity(); ///< World-to-clip transform shared by every draw.
};

static_assert(sizeof(FrameUniforms) == 16 * sizeof(float));
static_assert(alignof(FrameUniforms) == 16);
/** @endcond */
} // namespace fire_engine::detail
