#include "procedural_shadow_receiver.hpp"

#include <fire_engine/content/scene_content.hpp>
#include <fire_engine/graphics/material.hpp>
#include <fire_engine/graphics/mesh.hpp>
#include <fire_engine/graphics/render_object.hpp>
#include <fire_engine/math/transform.hpp>
#include <fire_engine/scene/scene_draw_list.hpp>
#include <fire_engine/scene/scene_node.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace
{
constexpr fire_engine::Color4 kWhite{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f};
constexpr std::array<fire_engine::Vertex, 4> kExpectedVertices{{
    {.position = {.x = -4.0f, .y = 0.0f, .z = -4.0f},
     .color = kWhite,
     .textureCoordinate = {.x = 0.0f, .y = 0.0f}},
    {.position = {.x = -4.0f, .y = 0.0f, .z = 4.0f},
     .color = kWhite,
     .textureCoordinate = {.x = 0.0f, .y = 1.0f}},
    {.position = {.x = 4.0f, .y = 0.0f, .z = 4.0f},
     .color = kWhite,
     .textureCoordinate = {.x = 1.0f, .y = 1.0f}},
    {.position = {.x = 4.0f, .y = 0.0f, .z = -4.0f},
     .color = kWhite,
     .textureCoordinate = {.x = 1.0f, .y = 0.0f}},
}};
void requireVertex(const fire_engine::Vertex& actual, const fire_engine::Vertex& expected)
{
    REQUIRE(actual.position == expected.position);
    REQUIRE(actual.color == expected.color);
    REQUIRE(actual.textureCoordinate == expected.textureCoordinate);
}

void requirePositiveYWinding(const fire_engine::Mesh& mesh, std::size_t firstIndex)
{
    const fire_engine::Vec3& first = mesh.vertices[mesh.indices[firstIndex]].position;
    const fire_engine::Vec3& second = mesh.vertices[mesh.indices[firstIndex + 1]].position;
    const fire_engine::Vec3& third = mesh.vertices[mesh.indices[firstIndex + 2]].position;
    REQUIRE((second - first).cross(third - first).y > 0.0f);
}
} // namespace

TEST_CASE("Procedural shadow receiver appends the registered receive-only plane")
{
    fire_engine::SceneContent content;
    const fire_engine::RenderObjectId receiver =
        fire_engine::tutorial::addProceduralShadowReceiver(content);

    REQUIRE(receiver == fire_engine::RenderObjectId{.value = 0});
    REQUIRE(content.assets.meshes().size() == 1);
    REQUIRE(content.assets.materials().size() == 1);
    REQUIRE(content.assets.renderObjects().size() == 1);
    REQUIRE(content.scene.roots().size() == 1);

    const fire_engine::Mesh& mesh = content.assets.meshes().front();
    REQUIRE(mesh.vertexLayout == fire_engine::VertexLayoutKey::ePositionColorTextureCoordinate);
    REQUIRE(mesh.vertices.size() == kExpectedVertices.size());
    for (std::size_t index = 0; index < kExpectedVertices.size(); ++index)
    {
        requireVertex(mesh.vertices[index], kExpectedVertices[index]);
    }
    REQUIRE(mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    requirePositiveYWinding(mesh, 0);
    requirePositiveYWinding(mesh, 3);

    const fire_engine::Material& material = content.assets.materials().front();
    REQUIRE(material.baseColor == kWhite);
    REQUIRE_FALSE(material.baseColorTexture.has_value());

    const fire_engine::RenderObject& object = content.assets.renderObjects().front();
    REQUIRE(object.mesh == fire_engine::MeshId{.value = 0});
    REQUIRE(object.material == fire_engine::MaterialId{.value = 0});
    REQUIRE_FALSE(object.castsShadow);

    const fire_engine::Transform expectedTransform{
        .translation = {.x = 0.0f, .y = -1.0f, .z = 0.0f},
    };
    const fire_engine::SceneNode& root = *content.scene.roots().front();
    REQUIRE(root.name() == "Procedural shadow receiver");
    REQUIRE(root.localTransform() == expectedTransform);
    REQUIRE(std::holds_alternative<fire_engine::RenderObjectId>(root.component()));
    REQUIRE(std::get<fire_engine::RenderObjectId>(root.component()) == receiver);

    content.scene.updateWorldTransforms();
    fire_engine::SceneDrawListArena drawListArena;
    const fire_engine::SceneDrawList drawList = content.scene.buildDrawItems(drawListArena);
    REQUIRE(drawList.drawItems.size() == 1);
    REQUIRE(drawList.drawItems.front().renderObject == receiver);
    REQUIRE(drawList.drawItems.front().world == expectedTransform.matrix());
}

TEST_CASE("Procedural shadow receiver appends without replacing existing content")
{
    fire_engine::SceneContent content;
    const fire_engine::MeshId existingMesh = content.assets.addMesh({
        .vertices = {kExpectedVertices[0], kExpectedVertices[1], kExpectedVertices[2]},
        .indices = {0, 1, 2},
    });
    const fire_engine::MaterialId existingMaterial = content.assets.addMaterial({
        .baseColor = {.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f},
        .baseColorTexture = std::nullopt,
    });
    const fire_engine::RenderObjectId existingObject = content.assets.addRenderObject({
        .mesh = existingMesh,
        .material = existingMaterial,
    });
    content.scene.addRoot("Existing root").component(existingObject);

    const fire_engine::RenderObjectId receiver =
        fire_engine::tutorial::addProceduralShadowReceiver(content);

    REQUIRE(receiver == fire_engine::RenderObjectId{.value = 1});
    REQUIRE(content.assets.meshes().size() == 2);
    REQUIRE(content.assets.materials().size() == 2);
    REQUIRE(content.assets.renderObjects().size() == 2);
    REQUIRE(content.assets.renderObjects().front().mesh == existingMesh);
    REQUIRE(content.assets.renderObjects().front().material == existingMaterial);
    REQUIRE(content.assets.renderObjects().front().castsShadow);
    REQUIRE(content.scene.roots().size() == 2);
    REQUIRE(content.scene.roots().front()->name() == "Existing root");
    REQUIRE(content.scene.roots().back()->name() == "Procedural shadow receiver");

    content.scene.updateWorldTransforms();
    fire_engine::SceneDrawListArena drawListArena;
    const fire_engine::SceneDrawList drawList = content.scene.buildDrawItems(drawListArena);
    REQUIRE(drawList.drawItems.size() == 2);
    REQUIRE(drawList.drawItems[0].renderObject == existingObject);
    REQUIRE(drawList.drawItems[1].renderObject == receiver);
}
