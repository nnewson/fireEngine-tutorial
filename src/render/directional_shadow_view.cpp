#include <fire_engine/render/directional_shadow_view.hpp>

#include <fire_engine/math/normalize_error.hpp>

#include <expected>
#include <stdexcept>

namespace fire_engine
{
/* --- Public free functions --- */

Mat4 directionalShadowViewProjection(const DirectionalShadowView& shadowView)
{
    const std::expected<Mat4, NormalizeError> view =
        Mat4::lookAt(shadowView.eye, shadowView.target, shadowView.up);
    if (!view.has_value())
    {
        throw std::invalid_argument("Directional shadow values produce a degenerate view basis");
    }

    const Mat4 projection =
        Mat4::orthographic(shadowView.left, shadowView.right, shadowView.bottom, shadowView.top,
                           shadowView.nearPlane, shadowView.farPlane);
    return projection * *view;
}
} // namespace fire_engine
