#pragma once

#include <cstddef>
#include <type_traits>

#include <fire_engine/math/mat4.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Per-draw model transform consumed by the depth-only shadow vertex stage. */
struct alignas(16) ShadowDrawConstants
{
    Mat4 model = Mat4::identity(); ///< Object-to-world transform for one shadow caster.
};

static_assert(sizeof(ShadowDrawConstants) == 16 * sizeof(float));
static_assert(alignof(ShadowDrawConstants) == 16);
static_assert(offsetof(ShadowDrawConstants, model) == 0);
static_assert(std::is_standard_layout_v<ShadowDrawConstants>);
static_assert(std::is_trivially_copyable_v<ShadowDrawConstants>);
/** @endcond */
} // namespace fire_engine::detail
