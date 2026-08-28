#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <numbers>
#include <stdexcept>

#include <fire_engine/math/quaternion.hpp>
#include <fire_engine/math/vec3.hpp>
#include <fire_engine/math/vec4.hpp>

namespace fire_engine
{
/* --- Classes --- */

/**
 * @brief Column-major four-by-four matrix shared by scene transforms and shaders.
 *
 * Column-major storage matches Slang's configured matrix layout, so a Mat4 can
 * cross the CPU/shader boundary without a transpose or repacking step.
 */
class alignas(16) Mat4 final
{
public:
    /** @brief Creates the zero matrix. */
    constexpr Mat4() noexcept = default;

    /**
     * @brief Returns the multiplicative identity matrix.
     * @return Matrix with ones on its diagonal and zeros elsewhere.
     */
    [[nodiscard]] static constexpr Mat4 identity() noexcept
    {
        Mat4 result;
        result[0, 0] = 1.0f;
        result[1, 1] = 1.0f;
        result[2, 2] = 1.0f;
        result[3, 3] = 1.0f;
        return result;
    }

    /**
     * @brief Builds an affine translation matrix.
     * @param translationValue Translation placed in the final column.
     * @return Matrix that translates positions by the supplied vector.
     */
    [[nodiscard]] static constexpr Mat4 translation(Vec3 translationValue) noexcept
    {
        Mat4 result = identity();
        result[0, 3] = translationValue.x;
        result[1, 3] = translationValue.y;
        result[2, 3] = translationValue.z;
        return result;
    }

    /**
     * @brief Builds an affine non-uniform scale matrix.
     * @param scaleValue Scale placed along the first three diagonal entries.
     * @return Matrix that scales positions by the supplied vector.
     */
    [[nodiscard]] static constexpr Mat4 scale(Vec3 scaleValue) noexcept
    {
        Mat4 result = identity();
        result[0, 0] = scaleValue.x;
        result[1, 1] = scaleValue.y;
        result[2, 2] = scaleValue.z;
        return result;
    }

    /**
     * @brief Builds a rotation matrix from a unit quaternion.
     * @param rotationValue Normalized quaternion rotation.
     * @return Matrix that applies the supplied rotation.
     */
    [[nodiscard]] static constexpr Mat4 rotation(Quaternion rotationValue) noexcept
    {
        const float xx = rotationValue.x * rotationValue.x;
        const float yy = rotationValue.y * rotationValue.y;
        const float zz = rotationValue.z * rotationValue.z;
        const float xy = rotationValue.x * rotationValue.y;
        const float xz = rotationValue.x * rotationValue.z;
        const float yz = rotationValue.y * rotationValue.z;
        const float xw = rotationValue.x * rotationValue.w;
        const float yw = rotationValue.y * rotationValue.w;
        const float zw = rotationValue.z * rotationValue.w;

        Mat4 result = identity();
        result[0, 0] = 1.0f - 2.0f * (yy + zz);
        result[0, 1] = 2.0f * (xy - zw);
        result[0, 2] = 2.0f * (xz + yw);
        result[1, 0] = 2.0f * (xy + zw);
        result[1, 1] = 1.0f - 2.0f * (xx + zz);
        result[1, 2] = 2.0f * (yz - xw);
        result[2, 0] = 2.0f * (xz - yw);
        result[2, 1] = 2.0f * (yz + xw);
        result[2, 2] = 1.0f - 2.0f * (xx + yy);
        return result;
    }

    /**
     * @brief Builds a right-handed perspective projection with zero-to-one depth.
     * @param verticalFieldOfView Vertical field of view in radians.
     * @param aspectRatio Framebuffer width divided by height.
     * @param nearPlane Positive distance to the near clipping plane.
     * @param farPlane Distance to the far clipping plane, greater than nearPlane.
     * @return Projection whose normalized depth range is zero to one.
     * @throws std::invalid_argument if a projection parameter is outside its valid range.
     *
     * Invalid projection configuration is a setup error. A framebuffer-derived aspect ratio
     * must only be supplied after resize handling has established a non-zero extent.
     */
    [[nodiscard]] static Mat4 perspective(float verticalFieldOfView, float aspectRatio,
                                          float nearPlane, float farPlane)
    {
        if (!std::isfinite(verticalFieldOfView) || !std::isfinite(aspectRatio) ||
            !std::isfinite(nearPlane) || !std::isfinite(farPlane) || verticalFieldOfView <= 0.0f ||
            verticalFieldOfView >= std::numbers::pi_v<float> || aspectRatio <= 0.0f ||
            nearPlane <= 0.0f || farPlane <= nearPlane)
        {
            throw std::invalid_argument("Perspective projection parameters are invalid");
        }

        const float focalLength = 1.0f / std::tan(verticalFieldOfView * 0.5f);
        Mat4 result;
        result[0, 0] = focalLength / aspectRatio;
        result[1, 1] = focalLength;
        result[2, 2] = farPlane / (nearPlane - farPlane);
        result[2, 3] = farPlane * nearPlane / (nearPlane - farPlane);
        result[3, 2] = -1.0f;
        return result;
    }

    /**
     * @brief Builds a right-handed view matrix looking from eye toward target.
     * @param eye Camera position in world space.
     * @param target World-space point at the center of the view.
     * @param up Approximate world-space up direction.
     * @return View matrix, or the normalization error caused by a degenerate basis.
     *
     * Unlike fixed projection configuration, a runtime camera direction can legitimately
     * become degenerate, so this factory reports that outcome explicitly.
     */
    [[nodiscard]] static std::expected<Mat4, NormalizeError> lookAt(Vec3 eye, Vec3 target,
                                                                    Vec3 up) noexcept
    {
        const std::expected<Vec3, NormalizeError> forwardResult = (target - eye).normalized();
        if (!forwardResult)
        {
            return std::unexpected{forwardResult.error()};
        }

        const Vec3 forward = *forwardResult;
        const std::expected<Vec3, NormalizeError> rightResult = forward.cross(up).normalized();
        if (!rightResult)
        {
            return std::unexpected{rightResult.error()};
        }

        const Vec3 right = *rightResult;
        const Vec3 cameraUp = right.cross(forward);

        Mat4 result = identity();
        result[0, 0] = right.x;
        result[0, 1] = right.y;
        result[0, 2] = right.z;
        result[0, 3] = -right.dot(eye);
        result[1, 0] = cameraUp.x;
        result[1, 1] = cameraUp.y;
        result[1, 2] = cameraUp.z;
        result[1, 3] = -cameraUp.dot(eye);
        result[2, 0] = -forward.x;
        result[2, 1] = -forward.y;
        result[2, 2] = -forward.z;
        result[2, 3] = forward.dot(eye);
        return result;
    }

    /**
     * @brief Reads one matrix element.
     * @param rowIndex Zero-based row index.
     * @param columnIndex Zero-based column index.
     * @return Element stored at the requested row and column.
     */
    [[nodiscard]] constexpr float operator[](std::size_t rowIndex,
                                             std::size_t columnIndex) const noexcept
    {
        return values_[columnIndex * 4 + rowIndex];
    }

    /**
     * @brief Returns one mutable matrix element.
     * @param rowIndex Zero-based row index.
     * @param columnIndex Zero-based column index.
     * @return Reference to the requested element.
     */
    [[nodiscard]] constexpr float& operator[](std::size_t rowIndex,
                                              std::size_t columnIndex) noexcept
    {
        return values_[columnIndex * 4 + rowIndex];
    }

    /**
     * @brief Returns one matrix row as a vector.
     * @param rowIndex Zero-based row index.
     * @return Four elements in increasing column order.
     */
    [[nodiscard]] constexpr Vec4 row(std::size_t rowIndex) const noexcept
    {
        return {
            .x = (*this)[rowIndex, 0],
            .y = (*this)[rowIndex, 1],
            .z = (*this)[rowIndex, 2],
            .w = (*this)[rowIndex, 3],
        };
    }

    /**
     * @brief Returns one matrix column as a vector.
     * @param columnIndex Zero-based column index.
     * @return Four elements in increasing row order.
     */
    [[nodiscard]] constexpr Vec4 column(std::size_t columnIndex) const noexcept
    {
        return {
            .x = (*this)[0, columnIndex],
            .y = (*this)[1, columnIndex],
            .z = (*this)[2, columnIndex],
            .w = (*this)[3, columnIndex],
        };
    }

    /**
     * @brief Returns the contiguous column-major float storage.
     * @return Pointer to the first of sixteen floats owned by this matrix.
     */
    [[nodiscard]] constexpr const float* data() const noexcept
    {
        return values_.data();
    }

    /**
     * @brief Multiplies this matrix by another matrix.
     * @param right Matrix applied before this matrix to a column vector.
     * @return Composed matrix.
     */
    [[nodiscard]] constexpr Mat4 operator*(const Mat4& right) const noexcept
    {
        Mat4 result;
        for (std::size_t rowIndex = 0; rowIndex < 4; ++rowIndex)
        {
            for (std::size_t columnIndex = 0; columnIndex < 4; ++columnIndex)
            {
                result[rowIndex, columnIndex] = row(rowIndex).dot(right.column(columnIndex));
            }
        }
        return result;
    }

    /**
     * @brief Multiplies this matrix by a column vector.
     * @param right Vector transformed by this matrix.
     * @return Transformed vector.
     */
    [[nodiscard]] constexpr Vec4 operator*(Vec4 right) const noexcept
    {
        return {
            .x = row(0).dot(right),
            .y = row(1).dot(right),
            .z = row(2).dot(right),
            .w = row(3).dot(right),
        };
    }

    /** @brief Compares all sixteen stored elements exactly. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const Mat4&) const noexcept = default;

private:
    std::array<float, 16> values_{}; ///< Column-major elements.
};

static_assert(sizeof(Mat4) == 16 * sizeof(float));
static_assert(alignof(Mat4) == 16);
} // namespace fire_engine
