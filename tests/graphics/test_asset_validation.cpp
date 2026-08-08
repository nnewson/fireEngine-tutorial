#include "fire_engine/graphics/detail/asset_validation.hpp"

#include "fire_engine/graphics/render_assets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
using fire_engine::Color4;
using fire_engine::Material;
using fire_engine::MaterialId;
using fire_engine::Mesh;
using fire_engine::MeshId;
using fire_engine::RenderAssets;
using fire_engine::RenderObject;
using fire_engine::Vec3;
using fire_engine::Vertex;

[[nodiscard]] Mesh makeTriangle()
{
    return {
        .vertices =
            {
                Vertex{.position = Vec3{.x = -0.5f}, .color = {}},
                Vertex{.position = Vec3{.x = 0.5f}, .color = {}},
                Vertex{.position = Vec3{.y = 0.5f}, .color = {}},
            },
        .indices = {0, 1, 2},
    };
}
} // namespace

TEST_CASE("Asset validation accepts complete descriptions")
{
    RenderAssets assets;
    const MeshId mesh = assets.addMesh(makeTriangle());
    const MaterialId material = assets.addMaterial(Material{});
    static_cast<void>(assets.addRenderObject(RenderObject{.mesh = mesh, .material = material}));

    REQUIRE_NOTHROW(fire_engine::detail::validateAssets(assets));
}

TEST_CASE("Asset validation rejects incomplete geometry")
{
    RenderAssets assets;
    Mesh mesh = makeTriangle();

    SECTION("empty vertices")
    {
        mesh.vertices.clear();
    }
    SECTION("empty indices")
    {
        mesh.indices.clear();
    }
    SECTION("incomplete triangle")
    {
        mesh.indices.pop_back();
    }
    SECTION("out-of-range index")
    {
        mesh.indices.back() = 3;
    }

    static_cast<void>(assets.addMesh(std::move(mesh)));
    REQUIRE_THROWS_AS(fire_engine::detail::validateAssets(assets), std::invalid_argument);
}

TEST_CASE("Asset validation rejects non-finite material colors")
{
    constexpr float kNotFinite = std::numeric_limits<float>::infinity();
    constexpr std::array invalidColors = {
        Color4{.r = kNotFinite},
        Color4{.g = kNotFinite},
        Color4{.b = kNotFinite},
        Color4{.a = kNotFinite},
    };

    for (const Color4 color : invalidColors)
    {
        RenderAssets assets;
        static_cast<void>(assets.addMaterial(Material{.baseColor = color}));
        REQUIRE_THROWS_AS(fire_engine::detail::validateAssets(assets), std::invalid_argument);
    }
}

TEST_CASE("Asset validation rejects missing render-object resources")
{
    RenderAssets assets;
    const MeshId mesh = assets.addMesh(makeTriangle());
    const MaterialId material = assets.addMaterial(Material{});

    SECTION("invalid mesh ID")
    {
        static_cast<void>(
            assets.addRenderObject(RenderObject{.mesh = MeshId{}, .material = material}));
    }
    SECTION("out-of-range mesh ID")
    {
        static_cast<void>(
            assets.addRenderObject(RenderObject{.mesh = MeshId{.value = 1}, .material = material}));
    }
    SECTION("invalid material ID")
    {
        static_cast<void>(
            assets.addRenderObject(RenderObject{.mesh = mesh, .material = MaterialId{}}));
    }
    SECTION("out-of-range material ID")
    {
        static_cast<void>(
            assets.addRenderObject(RenderObject{.mesh = mesh, .material = MaterialId{.value = 1}}));
    }

    REQUIRE_THROWS_AS(fire_engine::detail::validateAssets(assets), std::invalid_argument);
}
