#pragma once

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Three-component floating-point vector used by CPU scene data. */
struct Vec3
{
    float x = 0.0f; ///< First component.
    float y = 0.0f; ///< Second component.
    float z = 0.0f; ///< Third component.

    /** @brief Compares all three components exactly. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};
} // namespace fire_engine
