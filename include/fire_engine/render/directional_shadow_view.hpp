#pragma once

#include <fire_engine/math/mat4.hpp>
#include <fire_engine/math/vec3.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Vulkan-free directional-shadow view values sampled for one frame. */
struct DirectionalShadowView
{
    Vec3 eye;        ///< Directional-light view position in world space.
    Vec3 target;     ///< World-space point at the center of the shadow view.
    Vec3 up;         ///< Approximate world-space up direction.
    float left;      ///< Left orthographic view-space bound.
    float right;     ///< Right orthographic view-space bound.
    float bottom;    ///< Bottom orthographic view-space bound.
    float top;       ///< Top orthographic view-space bound.
    float nearPlane; ///< Positive distance to the near clipping plane.
    float farPlane;  ///< Far clipping distance, greater than nearPlane.
};

/* --- Free functions --- */

/**
 * @brief Resolves a directional shadow view into one right-handed world-to-clip transform.
 * @param shadowView Application-owned shadow values sampled for the frame.
 * @return Orthographic view-projection transform with zero-to-one normalized depth.
 * @throws std::invalid_argument if the view basis or projection values are invalid.
 */
[[nodiscard]] Mat4 directionalShadowViewProjection(const DirectionalShadowView& shadowView);
} // namespace fire_engine
