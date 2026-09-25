#pragma once

#include <cstddef>

#include <fire_engine/math/mat4.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/**
 * @brief Per-frame values shared by the tutorial render passes.
 *
 * Slang declares the matching constant buffer with Std140DataLayout. Each 4x4
 * float matrix occupies 64 bytes and has 16-byte base alignment in that layout.
 */
struct alignas(16) FrameUniforms
{
    Mat4 viewProjection = Mat4::identity();      ///< Forward world-to-clip transform.
    Mat4 lightViewProjection = Mat4::identity(); ///< Directional-shadow world-to-clip transform.
};

static_assert(sizeof(FrameUniforms) == 32 * sizeof(float));
static_assert(alignof(FrameUniforms) == 16);
static_assert(offsetof(FrameUniforms, viewProjection) == 0);
static_assert(offsetof(FrameUniforms, lightViewProjection) == 16 * sizeof(float));
/** @endcond */
} // namespace fire_engine::detail
