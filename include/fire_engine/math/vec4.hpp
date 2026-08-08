#pragma once

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Four-component floating-point vector used by colors and homogeneous coordinates. */
struct Vec4
{
    float x = 0.0f; ///< First component.
    float y = 0.0f; ///< Second component.
    float z = 0.0f; ///< Third component.
    float w = 0.0f; ///< Fourth component.

    /**
     * @brief Computes the four-component dot product with another vector.
     * @param right Vector whose matching components are multiplied.
     * @return Sum of the four component products.
     */
    [[nodiscard]] constexpr float dot(Vec4 right) const noexcept
    {
        return x * right.x + y * right.y + z * right.z + w * right.w;
    }

    /** @brief Compares all four components exactly. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const Vec4&) const noexcept = default;
};
} // namespace fire_engine
