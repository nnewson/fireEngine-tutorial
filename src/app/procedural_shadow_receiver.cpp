#include "procedural_shadow_receiver.hpp"

#include <fire_engine/content/scene_content.hpp>
#include <fire_engine/graphics/material.hpp>
#include <fire_engine/graphics/mesh.hpp>
#include <fire_engine/graphics/render_object.hpp>
#include <fire_engine/math/transform.hpp>
#include <fire_engine/scene/scene_node.hpp>

#include <optional>

namespace fire_engine::tutorial
{
/** @cond INTERNAL */
/* --- Public free functions --- */

RenderObjectId addProceduralShadowReceiver(SceneContent& content)
{
    constexpr Color4 kWhite{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f};
    const MeshId mesh = content.assets.addMesh({
        .vertices =
            {
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
            },
        .indices = {0, 1, 2, 0, 2, 3},
        .vertexLayout = VertexLayoutKey::ePositionColorTextureCoordinate,
    });
    const MaterialId material = content.assets.addMaterial({
        .baseColor = kWhite,
        .baseColorTexture = std::nullopt,
    });
    const RenderObjectId object = content.assets.addRenderObject({
        .mesh = mesh,
        .material = material,
        .castsShadow = false,
    });

    SceneNode& root = content.scene.addRoot("Procedural shadow receiver");
    root.localTransform(Transform{.translation = {.x = 0.0f, .y = -1.0f, .z = 0.0f}});
    root.component(object);
    return object;
}

/** @endcond */
} // namespace fire_engine::tutorial
