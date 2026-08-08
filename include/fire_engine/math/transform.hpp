#pragma once

#include <fire_engine/math/mat4.hpp>
#include <fire_engine/math/quaternion.hpp>
#include <fire_engine/math/vec3.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Decomposed three-stage TRS transform suitable for independent animation channels. */
struct Transform
{
    Vec3 translation{};                           ///< Position relative to the parent.
    Quaternion rotation = Quaternion::identity(); ///< Rotation relative to the parent.
    Vec3 scale{.x = 1.0f, .y = 1.0f, .z = 1.0f};  ///< Scale relative to the parent.

    /**
     * @brief Composes the three-stage local matrix in glTF translation, rotation, scale order.
     * @return Matrix that applies scale, then rotation, then translation to a point.
     */
    [[nodiscard]] constexpr Mat4 matrix() const noexcept
    {
        return Mat4::translation(translation) * Mat4::rotation(rotation) * Mat4::scale(scale);
    }

    /** @brief Compares every transform component exactly. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const Transform&) const noexcept = default;
};
} // namespace fire_engine
