#include "snapshot_acceptance.hpp"

#include <fire_engine/graphics/color4.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
using fire_engine::Color4;
using fire_engine::ImageData;
using fire_engine::Material;
using fire_engine::Mesh;
using fire_engine::RenderAssets;
using fire_engine::RenderObject;
using fire_engine::RenderObjectId;
using fire_engine::Texture;
using fire_engine::Vertex;

/** @brief Builds a catalog with distinguishable entries of every kind. */
RenderAssets sampleCatalog()
{
    RenderAssets assets;
    const auto image =
        assets.addImage(ImageData{.width = 1, .height = 1, .pixels = {10, 20, 30, 40}});
    const auto texture = assets.addTexture(Texture{.image = image});
    const auto mesh =
        assets.addMesh(Mesh{.vertices = {Vertex{}, Vertex{}, Vertex{}}, .indices = {0, 1, 2}});
    const auto plain = assets.addMaterial(
        Material{.baseColor = Color4{.r = 0.5f, .a = 1.0f}, .baseColorTexture = std::nullopt});
    const auto textured = assets.addMaterial(Material{.baseColorTexture = texture});
    static_cast<void>(
        assets.addRenderObject(RenderObject{.mesh = mesh, .material = plain, .castsShadow = true}));
    static_cast<void>(assets.addRenderObject(
        RenderObject{.mesh = mesh, .material = textured, .castsShadow = false}));
    return assets;
}
} // namespace

TEST_CASE("Appending a render object reuses an existing object's mesh and material")
{
    RenderAssets assets = sampleCatalog();
    const std::size_t revision = assets.revision();
    const RenderObject source = assets.renderObjects().at(1);

    const RenderObjectId appended =
        fire_engine::tutorial::appendRenderObjectLike(assets, RenderObjectId{.value = 1});

    REQUIRE(appended == RenderObjectId{.value = 2});
    REQUIRE(assets.revision() == revision + 1);
    REQUIRE(assets.meshes().size() == 1);
    REQUIRE(assets.images().size() == 1);
    const RenderObject& copy = assets.renderObjects().at(appended.value);
    REQUIRE(copy.mesh == source.mesh);
    REQUIRE(copy.material == source.material);
    REQUIRE(copy.castsShadow == source.castsShadow);
    REQUIRE_THROWS_AS(
        fire_engine::tutorial::appendRenderObjectLike(assets, RenderObjectId{.value = 9}),
        std::out_of_range);
}

TEST_CASE("Duplicating a catalog reproduces its entries and revision under a new identity")
{
    const RenderAssets source = sampleCatalog();

    const RenderAssets copy = fire_engine::tutorial::duplicateCatalog(source);

    REQUIRE(copy.id() != source.id());
    REQUIRE(copy.revision() == source.revision());
    REQUIRE(copy.images().size() == source.images().size());
    REQUIRE(copy.images().front().pixels == source.images().front().pixels);
    REQUIRE(copy.textures().front().image == source.textures().front().image);
    REQUIRE(copy.meshes().front().indices == source.meshes().front().indices);
    REQUIRE(copy.materials().size() == source.materials().size());
    REQUIRE(copy.materials().front().baseColor == source.materials().front().baseColor);
    REQUIRE(copy.materials().back().baseColorTexture == source.materials().back().baseColorTexture);
    REQUIRE(copy.renderObjects().size() == source.renderObjects().size());
    for (std::size_t index = 0; index < source.renderObjects().size(); ++index)
    {
        REQUIRE(copy.renderObjects()[index].mesh == source.renderObjects()[index].mesh);
        REQUIRE(copy.renderObjects()[index].material == source.renderObjects()[index].material);
        REQUIRE(copy.renderObjects()[index].castsShadow ==
                source.renderObjects()[index].castsShadow);
    }
}
