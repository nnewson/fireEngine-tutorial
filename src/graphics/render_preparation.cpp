#include <fire_engine/graphics/render_preparation.hpp>

#include <fire_engine/graphics/detail/asset_validation.hpp>
#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/scene/scene_draw_list.hpp>

#include <stdexcept>
#include <utility>
#include <vector>

namespace fire_engine
{
namespace
{
/** @cond INTERNAL */
/* --- File-local function declarations --- */

/**
 * @brief Extracts the exact ordered dependency key retained beside its hash.
 * @param drawList Draw items whose render-object references form the key.
 * @return Render-object IDs in draw order, including repeated references.
 */
[[nodiscard]] std::vector<RenderObjectId> dependencies(const SceneDrawList& drawList);
/** @endcond */
} // namespace

/* --- Public member functions --- */

const RenderPreparationPlan& RenderPreparation::build(const RenderAssets& assets,
                                                      const SceneDrawList& drawList)
{
    const bool assetsChanged = validatedAssets_ != &assets || !validatedRevision_.has_value() ||
                               *validatedRevision_ != assets.revision();
    if (assetsChanged)
    {
        detail::validateAssets(assets);
        validatedAssets_ = &assets;
        validatedRevision_ = assets.revision();
    }

    std::vector<RenderObjectId> currentDependencies = dependencies(drawList);
    if (!assetsChanged && cachedPlan_.has_value() &&
        cachedPlan_->dependencyHash == drawList.dependencyHash &&
        cachedDependencies_ == currentDependencies)
    {
        return *cachedPlan_;
    }

    std::vector<bool> usedRenderObjects(assets.renderObjects().size(), false);
    for (const RenderObjectId renderObject : currentDependencies)
    {
        if (!renderObject.valid() || renderObject.value >= assets.renderObjects().size())
        {
            throw std::invalid_argument("A scene draw refers to a missing render object");
        }
        usedRenderObjects[renderObject.value] = true;
    }

    RenderPreparationPlan plan;
    plan.assetRevision = assets.revision();
    plan.dependencyHash = drawList.dependencyHash;
    std::vector<bool> usedMeshes(assets.meshes().size(), false);
    std::vector<bool> usedImages(assets.images().size(), false);
    std::vector<bool> usedTextures(assets.textures().size(), false);
    std::vector<bool> usedMaterials(assets.materials().size(), false);

    for (std::size_t index = 0; index < usedRenderObjects.size(); ++index)
    {
        if (!usedRenderObjects[index])
        {
            continue;
        }

        const RenderObject& renderObject = assets.renderObjects()[index];
        usedMeshes[renderObject.mesh.value] = true;
        usedMaterials[renderObject.material.value] = true;
        plan.renderObjects.push_back({
            .id = RenderObjectId{.value = index},
            .mesh = renderObject.mesh,
            .material = renderObject.material,
        });
    }

    for (std::size_t index = 0; index < usedMeshes.size(); ++index)
    {
        if (usedMeshes[index])
        {
            plan.meshes.push_back(MeshId{.value = index});
        }
    }
    for (std::size_t index = 0; index < usedMaterials.size(); ++index)
    {
        if (usedMaterials[index])
        {
            plan.materials.push_back(MaterialId{.value = index});
            const std::optional<TextureId> texture = assets.materials()[index].baseColorTexture;
            if (texture.has_value())
            {
                usedTextures[texture->value] = true;
            }
        }
    }
    for (std::size_t index = 0; index < usedTextures.size(); ++index)
    {
        if (usedTextures[index])
        {
            plan.textures.push_back(TextureId{.value = index});
            usedImages[assets.textures()[index].image.value] = true;
        }
    }
    for (std::size_t index = 0; index < usedImages.size(); ++index)
    {
        if (usedImages[index])
        {
            plan.images.push_back(ImageId{.value = index});
        }
    }

    cachedDependencies_ = std::move(currentDependencies);
    cachedPlan_ = std::move(plan);
    ++generation_;
    return *cachedPlan_;
}

std::size_t RenderPreparation::generation() const noexcept
{
    return generation_;
}

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

std::vector<RenderObjectId> dependencies(const SceneDrawList& drawList)
{
    std::vector<RenderObjectId> result;
    result.reserve(drawList.drawItems.size());
    for (const DrawItem& drawItem : drawList.drawItems)
    {
        result.push_back(drawItem.renderObject);
    }
    return result;
}
/** @endcond */
} // namespace
} // namespace fire_engine
