#include "tutorial_frame_descriptions.hpp"

#include <numbers>

namespace fire_engine::tutorial
{
namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

/** @brief Directional-shadow policy shared by both tutorial descriptions. */
constexpr DirectionalShadowView kDirectionalShadowView{
    .eye = {.x = -4.0f, .y = 6.0f, .z = 4.0f},
    .target = {.x = 0.0f, .y = -0.5f, .z = 0.0f},
    .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f},
    .left = -4.0f,
    .right = 4.0f,
    .bottom = -4.0f,
    .top = 4.0f,
    .nearPlane = 0.1f,
    .farPlane = 20.0f,
};

/** @brief Existing AnimatedCube perspective policy grouped with the registered shadow view. */
constexpr FrameDescription kAnimatedCubeFrameDescription{
    .camera =
        {
            .position = {.x = 0.0f, .y = 0.0f, .z = 4.0f},
            .target = {},
            .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f},
            .verticalFieldOfViewRadians = std::numbers::pi_v<float> / 3.0f,
            .nearPlane = 0.1f,
            .farPlane = 100.0f,
        },
    .directionalShadow = kDirectionalShadowView,
};

/** @brief Shadow-demonstration perspective policy grouped with the same shadow view. */
constexpr FrameDescription kShadowDemonstrationFrameDescription{
    .camera =
        {
            .position = {.x = 4.0f, .y = 3.0f, .z = 6.0f},
            .target = {.x = 0.0f, .y = -0.25f, .z = 0.0f},
            .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f},
            .verticalFieldOfViewRadians = std::numbers::pi_v<float> / 3.0f,
            .nearPlane = 0.1f,
            .farPlane = 100.0f,
        },
    .directionalShadow = kDirectionalShadowView,
};

/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Public free functions --- */

const FrameDescription& animatedCubeFrameDescription() noexcept
{
    return kAnimatedCubeFrameDescription;
}

const FrameDescription& shadowDemonstrationFrameDescription() noexcept
{
    return kShadowDemonstrationFrameDescription;
}

/** @endcond */
} // namespace fire_engine::tutorial
