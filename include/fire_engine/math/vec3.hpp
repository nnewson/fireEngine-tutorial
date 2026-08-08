#pragma once

#include <cmath>
#include <expected>

#include <fire_engine/math/normalize_error.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Three-component floating-point vector used by CPU scene data. */
struct Vec3
{
    float x = 0.0f; ///< First component.
    float y = 0.0f; ///< Second component.
    float z = 0.0f; ///< Third component.

    /**
     * @brief Computes the three-component dot product.
     * @param right Vector whose matching components are multiplied.
     * @return Sum of the three component products.
     */
    [[nodiscard]] constexpr float dot(Vec3 right) const noexcept
    {
        return x * right.x + y * right.y + z * right.z;
    }

    /**
     * @brief Computes the right-handed cross product.
     * @param right Second vector in the cross product.
     * @return Vector perpendicular to this vector and right.
     */
    [[nodiscard]] constexpr Vec3 cross(Vec3 right) const noexcept
    {
        return {
            .x = y * right.z - z * right.y,
            .y = z * right.x - x * right.z,
            .z = x * right.y - y * right.x,
        };
    }

    /**
     * @brief Returns the squared vector length without a square root.
     *
     * Unlike normalized(), this direct sum may overflow or underflow for extreme values.
     * @return Sum of squared components.
     */
    [[nodiscard]] constexpr float lengthSquared() const noexcept
    {
        return dot(*this);
    }

    /**
     * @brief Returns this vector with unit length.
     * @return Normalized vector, or the reason normalization was impossible.
     */
    [[nodiscard]] std::expected<Vec3, NormalizeError> normalized() const noexcept
    {
        // hypot is slower than sqrt(lengthSquared()), but avoids its avoidable overflow and
        // underflow. Normalization favors accuracy; lengthSquared() remains the faster comparison.
        const float magnitude = std::hypot(x, y, z);
        if (magnitude == 0.0f)
        {
            return std::unexpected{NormalizeError::eZeroLength};
        }
        if (!std::isfinite(magnitude))
        {
            return std::unexpected{NormalizeError::eNonFinite};
        }
        return Vec3{.x = x / magnitude, .y = y / magnitude, .z = z / magnitude};
    }

    /**
     * @brief Subtracts another vector component by component.
     * @param right Vector subtracted from this vector.
     * @return Component-wise difference.
     */
    [[nodiscard]] constexpr Vec3 operator-(Vec3 right) const noexcept
    {
        return {.x = x - right.x, .y = y - right.y, .z = z - right.z};
    }

    /** @brief Compares all three components exactly. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};
} // namespace fire_engine
