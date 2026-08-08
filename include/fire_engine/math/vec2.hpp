#pragma once

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Two-component floating-point vector used by CPU scene and texture data. */
struct Vec2
{
    float x = 0.0f; ///< First component.
    float y = 0.0f; ///< Second component.

    /** @brief Compares both components exactly. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const Vec2&) const noexcept = default;
};
} // namespace fire_engine
