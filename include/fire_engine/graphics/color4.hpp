#pragma once

#include <type_traits>

namespace fire_engine
{
/* --- POD structs --- */

/**
 * @brief Linear red, green, blue, and alpha colour components.
 *
 * Color4 is deliberately separate from Vec4: it shares the same four-float
 * storage shape for shader interfaces without exposing mathematical vector
 * operations that do not describe colour-domain behaviour.
 */
struct Color4
{
    float r = 0.0f; ///< Red component.
    float g = 0.0f; ///< Green component.
    float b = 0.0f; ///< Blue component.
    float a = 0.0f; ///< Alpha component.

    /** @brief Compares all four components exactly. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const Color4&) const noexcept = default;
};

static_assert(sizeof(Color4) == 4 * sizeof(float));
static_assert(std::is_aggregate_v<Color4>);
static_assert(std::is_standard_layout_v<Color4>);
static_assert(std::is_trivially_copyable_v<Color4>);
} // namespace fire_engine
