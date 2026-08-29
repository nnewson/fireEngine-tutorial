#include <fire_engine/render/camera.hpp>

#include <fire_engine/math/normalize_error.hpp>

#include <expected>
#include <stdexcept>

namespace fire_engine
{
/* --- Public free functions --- */

Mat4 cameraViewProjection(const Camera& camera, float aspectRatio)
{
    const std::expected<Mat4, NormalizeError> view =
        Mat4::lookAt(camera.position, camera.target, camera.up);
    if (!view.has_value())
    {
        throw std::invalid_argument("Camera values produce a degenerate view basis");
    }

    const Mat4 projection = Mat4::perspective(camera.verticalFieldOfViewRadians, aspectRatio,
                                              camera.nearPlane, camera.farPlane);
    return projection * *view;
}
} // namespace fire_engine
