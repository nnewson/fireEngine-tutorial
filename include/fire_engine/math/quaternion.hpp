#pragma once

#include <algorithm>
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
     * @brief Interpolates normalized rotations along their shortest spherical arc.
     *
     * Angular speed is constant within an interval, except for a normalized-linear
     * approximation for nearly equal rotations. A negative dot product negates the right
     * endpoint; an exactly zero dot product preserves the supplied half-turn direction.
     * Both endpoints are validated even when amount is zero or one.
     * @param right Finite, nonzero rotation reached when amount is one.
     * @param amount Interpolation fraction in the range zero to one.
     * @pre A finite amount lies in the range zero to one; extrapolation is not supported.
     * @return Unit quaternion, or a normalization error for an invalid endpoint or
     * non-finite amount.
     */
    [[nodiscard]] std::expected<Quaternion, NormalizeError>
    sphericalLerp(Quaternion right, float amount) const noexcept
    {
        if (!std::isfinite(amount))
        {
            return std::unexpected{NormalizeError::eNonFinite};
        }

        const auto left = normalized();
        if (!left.has_value())
        {
            return std::unexpected{left.error()};
        }
        const auto normalizedRight = right.normalized();
        if (!normalizedRight.has_value())
        {
            return std::unexpected{normalizedRight.error()};
        }
        right = *normalizedRight;

        float cosine = left->dot(right);
        if (cosine < 0.0f)
        {
            right = -right;
            cosine = -cosine;
        }
        if (amount == 0.0f)
        {
            return *left;
        }
        if (amount == 1.0f)
        {
            return right;
        }

        // Rounded unit-vector dot products can exceed one. Near equality, avoid dividing
        // by a vanishing sine. This threshold bounds the full rotation arc to about 3.6
        // degrees; tests straddle it and bound the approximation's component error.
        constexpr float kNearEqualDotThreshold = 0.9995f;
        cosine = std::clamp(cosine, 0.0f, 1.0f);
        if (cosine >= kNearEqualDotThreshold)
        {
            return left->normalizedLerp(right, amount);
        }

        const float angle = std::acos(cosine);
        const float sine = std::sin(angle);
        const float leftWeight = std::sin((1.0f - amount) * angle) / sine;
        const float rightWeight = std::sin(amount * angle) / sine;
        return Quaternion{
            .x = leftWeight * left->x + rightWeight * right.x,
            .y = leftWeight * left->y + rightWeight * right.y,
            .z = leftWeight * left->z + rightWeight * right.z,
            .w = leftWeight * left->w + rightWeight * right.w,
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
