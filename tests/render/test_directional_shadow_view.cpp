#include "fire_engine/render/directional_shadow_view.hpp"
#include "fire_engine/render/frame_description.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace
{
constexpr fire_engine::DirectionalShadowView kShadowView{
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
} // namespace

static_assert(std::is_aggregate_v<fire_engine::DirectionalShadowView>);
static_assert(std::is_standard_layout_v<fire_engine::DirectionalShadowView>);
static_assert(std::is_trivially_copyable_v<fire_engine::DirectionalShadowView>);
static_assert(std::is_aggregate_v<fire_engine::FrameDescription>);
static_assert(std::is_standard_layout_v<fire_engine::FrameDescription>);
static_assert(std::is_trivially_copyable_v<fire_engine::FrameDescription>);

TEST_CASE("Directional shadow view resolves application values into an orthographic projection")
{
    const fire_engine::Mat4 viewProjection =
        fire_engine::directionalShadowViewProjection(kShadowView);
    const auto view =
        fire_engine::Mat4::lookAt(kShadowView.eye, kShadowView.target, kShadowView.up);
    REQUIRE(view.has_value());
    const fire_engine::Mat4 projection = fire_engine::Mat4::orthographic(
        kShadowView.left, kShadowView.right, kShadowView.bottom, kShadowView.top,
        kShadowView.nearPlane, kShadowView.farPlane);
    REQUIRE(viewProjection == projection * *view);
}

TEST_CASE("Directional shadow view rejects degenerate view and projection values")
{
    fire_engine::DirectionalShadowView targetAtEye = kShadowView;
    targetAtEye.target = targetAtEye.eye;
    REQUIRE_THROWS_AS(fire_engine::directionalShadowViewProjection(targetAtEye),
                      std::invalid_argument);

    fire_engine::DirectionalShadowView collinearUp = kShadowView;
    collinearUp.eye = {};
    collinearUp.target = {.x = 0.0f, .y = 0.0f, .z = -1.0f};
    collinearUp.up = collinearUp.target;
    REQUIRE_THROWS_AS(fire_engine::directionalShadowViewProjection(collinearUp),
                      std::invalid_argument);

    fire_engine::DirectionalShadowView nonFiniteView = kShadowView;
    nonFiniteView.eye.x = std::numeric_limits<float>::infinity();
    REQUIRE_THROWS_AS(fire_engine::directionalShadowViewProjection(nonFiniteView),
                      std::invalid_argument);

    fire_engine::DirectionalShadowView invalidProjection = kShadowView;
    invalidProjection.left = invalidProjection.right;
    REQUIRE_THROWS_AS(fire_engine::directionalShadowViewProjection(invalidProjection),
                      std::invalid_argument);
}
