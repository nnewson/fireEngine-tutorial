#include "tutorial_frame_descriptions.hpp"

#include <fire_engine/render/camera.hpp>
#include <fire_engine/render/directional_shadow_view.hpp>

#include <catch2/catch_test_macros.hpp>

#include <numbers>

namespace
{
constexpr fire_engine::DirectionalShadowView kExpectedDirectionalShadow{
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

constexpr fire_engine::Camera kExpectedAnimatedCubeCamera{
    .position = {.x = 0.0f, .y = 0.0f, .z = 4.0f},
    .target = {},
    .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f},
    .verticalFieldOfViewRadians = std::numbers::pi_v<float> / 3.0f,
    .nearPlane = 0.1f,
    .farPlane = 100.0f,
};

constexpr fire_engine::Camera kExpectedShadowDemonstrationCamera{
    .position = {.x = 4.0f, .y = 3.0f, .z = 6.0f},
    .target = {.x = 0.0f, .y = -0.25f, .z = 0.0f},
    .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f},
    .verticalFieldOfViewRadians = std::numbers::pi_v<float> / 3.0f,
    .nearPlane = 0.1f,
    .farPlane = 100.0f,
};

void requireCamera(const fire_engine::Camera& actual, const fire_engine::Camera& expected)
{
    REQUIRE(actual.position == expected.position);
    REQUIRE(actual.target == expected.target);
    REQUIRE(actual.up == expected.up);
    REQUIRE(actual.verticalFieldOfViewRadians == expected.verticalFieldOfViewRadians);
    REQUIRE(actual.nearPlane == expected.nearPlane);
    REQUIRE(actual.farPlane == expected.farPlane);
}

void requireDirectionalShadow(const fire_engine::DirectionalShadowView& actual,
                              const fire_engine::DirectionalShadowView& expected)
{
    REQUIRE(actual.eye == expected.eye);
    REQUIRE(actual.target == expected.target);
    REQUIRE(actual.up == expected.up);
    REQUIRE(actual.left == expected.left);
    REQUIRE(actual.right == expected.right);
    REQUIRE(actual.bottom == expected.bottom);
    REQUIRE(actual.top == expected.top);
    REQUIRE(actual.nearPlane == expected.nearPlane);
    REQUIRE(actual.farPlane == expected.farPlane);
}
} // namespace

TEST_CASE("AnimatedCube frame description preserves the original camera and registered shadow view")
{
    const fire_engine::FrameDescription& description =
        fire_engine::tutorial::animatedCubeFrameDescription();

    requireCamera(description.camera, kExpectedAnimatedCubeCamera);
    requireDirectionalShadow(description.directionalShadow, kExpectedDirectionalShadow);
    REQUIRE_NOTHROW(fire_engine::cameraViewProjection(description.camera, 4.0f / 3.0f));
    REQUIRE_NOTHROW(fire_engine::directionalShadowViewProjection(description.directionalShadow));
}

TEST_CASE("Shadow demonstration changes only perspective camera policy")
{
    const fire_engine::FrameDescription& animated =
        fire_engine::tutorial::animatedCubeFrameDescription();
    const fire_engine::FrameDescription& demonstration =
        fire_engine::tutorial::shadowDemonstrationFrameDescription();

    requireCamera(demonstration.camera, kExpectedShadowDemonstrationCamera);
    requireDirectionalShadow(demonstration.directionalShadow, kExpectedDirectionalShadow);
    requireDirectionalShadow(demonstration.directionalShadow, animated.directionalShadow);
    REQUIRE_FALSE(demonstration.camera.position == animated.camera.position);
    REQUIRE_FALSE(demonstration.camera.target == animated.camera.target);
    REQUIRE(demonstration.camera.up == animated.camera.up);
    REQUIRE(demonstration.camera.verticalFieldOfViewRadians ==
            animated.camera.verticalFieldOfViewRadians);
    REQUIRE(demonstration.camera.nearPlane == animated.camera.nearPlane);
    REQUIRE(demonstration.camera.farPlane == animated.camera.farPlane);
    REQUIRE_NOTHROW(fire_engine::cameraViewProjection(demonstration.camera, 4.0f / 3.0f));
    REQUIRE_NOTHROW(fire_engine::directionalShadowViewProjection(demonstration.directionalShadow));
}
