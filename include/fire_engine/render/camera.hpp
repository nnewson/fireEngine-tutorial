#pragma once

#include <fire_engine/math/mat4.hpp>
#include <fire_engine/math/vec3.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Vulkan-free perspective-camera values sampled for one frame. */
struct Camera
{
    Vec3 position;                    ///< Eye position in world space.
    Vec3 target;                      ///< World-space point at the center of the view.
    Vec3 up;                          ///< Approximate world-space up direction.
    float verticalFieldOfViewRadians; ///< Vertical field of view in radians.
    float nearPlane;                  ///< Positive distance to the near clipping plane.
    float farPlane;                   ///< Far clipping distance, greater than nearPlane.
};

/* --- Free functions --- */

/**
 * @brief Resolves a perspective camera into one right-handed world-to-clip transform.
 * @param camera Application-owned camera values sampled for the frame.
 * @param aspectRatio Positive framebuffer width divided by height.
 * @return View-projection transform with zero-to-one normalized depth.
 * @throws std::invalid_argument if the view basis or projection values are invalid.
 */
[[nodiscard]] Mat4 cameraViewProjection(const Camera& camera, float aspectRatio);
} // namespace fire_engine
