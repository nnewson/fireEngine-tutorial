#include <fire_engine/render/detail/frame_uniforms.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

using fire_engine::Mat4;
using fire_engine::detail::FrameUniforms;

static_assert(std::is_aggregate_v<FrameUniforms>);
static_assert(std::is_standard_layout_v<FrameUniforms>);
static_assert(std::is_trivially_copyable_v<FrameUniforms>);
static_assert(sizeof(FrameUniforms) == 2 * sizeof(Mat4));
static_assert(alignof(FrameUniforms) == 16);
static_assert(offsetof(FrameUniforms, viewProjection) == 0);
static_assert(offsetof(FrameUniforms, lightViewProjection) == sizeof(Mat4));

TEST_CASE("Frame uniforms preserve the shared shader layout and defensive defaults")
{
    constexpr FrameUniforms uniforms{};

    REQUIRE(uniforms.viewProjection == Mat4::identity());
    REQUIRE(uniforms.lightViewProjection == Mat4::identity());
}
