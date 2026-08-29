#include "fire_engine/render/camera.hpp"

#include <catch2/catch_test_macros.hpp>

#include <numbers>
#include <stdexcept>

namespace
{
constexpr fire_engine::Camera kCamera{
    .position = {.x = 0.0f, .y = 0.0f, .z = 4.0f},
    .target = {},
    .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f},
    .verticalFieldOfViewRadians = std::numbers::pi_v<float> / 3.0f,
    .nearPlane = 0.1f,
    .farPlane = 100.0f,
};
} // namespace

TEST_CASE("Camera resolves application values into an extent-dependent projection")
{
    const fire_engine::Mat4 viewProjection =
        fire_engine::cameraViewProjection(kCamera, 4.0f / 3.0f);
    const auto view = fire_engine::Mat4::lookAt(kCamera.position, kCamera.target, kCamera.up);
    REQUIRE(view.has_value());
    const fire_engine::Mat4 projection = fire_engine::Mat4::perspective(
        kCamera.verticalFieldOfViewRadians, 4.0f / 3.0f, kCamera.nearPlane, kCamera.farPlane);
    REQUIRE(viewProjection == projection * *view);
}

TEST_CASE("Camera rejects degenerate view and projection values")
{
    fire_engine::Camera degenerateView = kCamera;
    degenerateView.target = degenerateView.position;
    REQUIRE_THROWS_AS(fire_engine::cameraViewProjection(degenerateView, 1.0f),
                      std::invalid_argument);

    fire_engine::Camera invalidProjection = kCamera;
    invalidProjection.nearPlane = invalidProjection.farPlane;
    REQUIRE_THROWS_AS(fire_engine::cameraViewProjection(invalidProjection, 1.0f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(fire_engine::cameraViewProjection(kCamera, 0.0f), std::invalid_argument);
}
