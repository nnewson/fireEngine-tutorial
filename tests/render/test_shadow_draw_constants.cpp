#include <fire_engine/render/detail/shadow_draw_constants.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

using fire_engine::Mat4;
using fire_engine::detail::ShadowDrawConstants;

static_assert(std::is_aggregate_v<ShadowDrawConstants>);
static_assert(std::is_standard_layout_v<ShadowDrawConstants>);
static_assert(std::is_trivially_copyable_v<ShadowDrawConstants>);
static_assert(sizeof(ShadowDrawConstants) == sizeof(Mat4));
static_assert(alignof(ShadowDrawConstants) == 16);
static_assert(offsetof(ShadowDrawConstants, model) == 0);

TEST_CASE("Shadow draw constants preserve the depth-only shader layout")
{
    constexpr ShadowDrawConstants constants{};

    REQUIRE(constants.model == Mat4::identity());
}
