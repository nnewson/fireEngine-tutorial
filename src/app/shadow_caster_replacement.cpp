#include "shadow_caster_replacement.hpp"

#include <fire_engine/content/scene_content.hpp>

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace fire_engine::tutorial
{
namespace
{
/** @cond INTERNAL */
/* --- File-local function declarations --- */

/**
 * @brief Finds a single casting instance across the complete scene traversal.
 * @param node Subtree currently visited.
 * @param assets Descriptions referenced by renderable nodes.
 * @param caster Selected node, or empty until a casting instance is found.
 * @throws std::logic_error if a second casting instance is found.
 * @throws std::out_of_range if a node references a missing render object.
 */
void findCaster(SceneNode& node, const RenderAssets& assets, std::optional<SceneNodeRef>& caster);

/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Public free functions --- */

ShadowCasterReplacement replaceShadowCaster(SceneContent& content)
{
    std::optional<SceneNodeRef> caster;
    for (const auto& root : content.scene.roots())
    {
        findCaster(*root, content.assets, caster);
    }
    if (!caster.has_value())
    {
        throw std::logic_error(
            "Shadow replacement fixture requires exactly one casting scene node");
    }

    SceneNode& node = caster->get();
    const std::optional<SceneNodeId> nodeId = node.id();
    if (!nodeId.has_value())
    {
        throw std::logic_error("The shadow replacement caster must have a scene identity");
    }
    const RenderObjectId originalId = std::get<RenderObjectId>(node.component());
    const RenderObject original = content.assets.renderObjects().at(originalId.value);
    // Copy before appending: asset insertion can invalidate references into the catalog.
    Mesh mesh = content.assets.meshes().at(original.mesh.value);
    Material material = content.assets.materials().at(original.material.value);
    for (Vertex& vertex : mesh.vertices)
    {
        vertex.position.x *= 0.75f;
    }
    material.baseColorTexture = std::nullopt;

    const MeshId meshId = content.assets.addMesh(std::move(mesh));
    const MaterialId materialId = content.assets.addMaterial(material);
    const RenderObjectId replacementId = content.assets.addRenderObject({
        .mesh = meshId,
        .material = materialId,
        .castsShadow = true,
    });
    node.component(replacementId);
    return {
        .node = *nodeId,
        .originalObject = originalId,
        .replacementObject = replacementId,
    };
}

namespace
{
/* --- File-local functions --- */

void findCaster(SceneNode& node, const RenderAssets& assets, std::optional<SceneNodeRef>& caster)
{
    const RenderObjectId* object = std::get_if<RenderObjectId>(&node.component());
    if (object != nullptr && assets.renderObjects().at(object->value).castsShadow)
    {
        if (caster.has_value())
        {
            throw std::logic_error(
                "Shadow replacement fixture requires exactly one casting scene node");
        }
        caster = std::ref(node);
    }
    for (const auto& child : node.children())
    {
        findCaster(*child, assets, caster);
    }
}
} // namespace
/** @endcond */
} // namespace fire_engine::tutorial
