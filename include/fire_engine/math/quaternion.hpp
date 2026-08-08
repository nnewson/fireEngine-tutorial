#pragma once

#include <cmath>
#include <expected>

#include <fire_engine/math/normalize_error.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Quaternion rotation stored in the same x, y, z, w order used by glTF. */
struct Quaternion
{
    float x = 0.0f; ///< Imaginary x component.
    float y = 0.0f; ///< Imaginary y component.
    float z = 0.0f; ///< Imaginary z component.
    float w = 1.0f; ///< Real component.

    /** @brief Returns the rotation identity. @return Quaternion with no rotation. */
    [[nodiscard]] static constexpr Quaternion identity() noexcept
    {
        return {};
    }

    /**
     * @brief Returns the squared quaternion length without a square root.
     *
     * Unlike normalized(), this direct sum may overflow or underflow for extreme values.
     * @return Sum of squared components.
     */
    [[nodiscard]] constexpr float lengthSquared() const noexcept
    {
        return x * x + y * y + z * z + w * w;
    }

    /**
     * @brief Returns this quaternion with unit length.
     * @return Normalized quaternion, or the reason normalization was impossible.
     */
    [[nodiscard]] std::expected<Quaternion, NormalizeError> normalized() const noexcept
    {
        // Pairwise hypot keeps all four components in a safe range. Its extra scaling work is
        // preferable for transforms; lengthSquared() remains available for faster comparisons.
        const float magnitude = std::hypot(std::hypot(x, y), std::hypot(z, w));
        if (magnitude == 0.0f)
        {
            return std::unexpected{NormalizeError::eZeroLength};
        }
        if (!std::isfinite(magnitude))
        {
            return std::unexpected{NormalizeError::eNonFinite};
        }
        return Quaternion{
            .x = x / magnitude,
            .y = y / magnitude,
            .z = z / magnitude,
            .w = w / magnitude,
        };
    }

    /**
     * @brief Interpolates along the shortest quaternion arc and normalizes the result.
     * @param right Rotation reached when amount is one.
     * @param amount Linear interpolation amount, normally in the range zero to one.
     * @return Unit quaternion, or the reason the interpolated value could not be normalized.
     */
    [[nodiscard]] std::expected<Quaternion, NormalizeError>
    normalizedLerp(Quaternion right, float amount) const noexcept
    {
        if (dot(right) < 0.0f)
        {
            right = -right;
        }

        return Quaternion{
            .x = x + (right.x - x) * amount,
            .y = y + (right.y - y) * amount,
            .z = z + (right.z - z) * amount,
            .w = w + (right.w - w) * amount,
        }
            .normalized();
    }

    /**
     * @brief Computes the quaternion dot product.
     * @param right Quaternion whose matching components are multiplied.
     * @return Sum of the four component products.
     */
    [[nodiscard]] constexpr float dot(Quaternion right) const noexcept
    {
        return x * right.x + y * right.y + z * right.z + w * right.w;
    }

    /**
     * @brief Negates every component without changing the represented rotation.
     * @return Equivalent quaternion on the opposite hemisphere.
     */
    [[nodiscard]] constexpr Quaternion operator-() const noexcept
    {
        return {.x = -x, .y = -y, .z = -z, .w = -w};
    }

    /** @brief Compares all four stored components exactly. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const Quaternion&) const noexcept = default;
};
} // namespace fire_engine
