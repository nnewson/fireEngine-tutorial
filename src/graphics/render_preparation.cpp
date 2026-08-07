#include <fire_engine/graphics/render_preparation.hpp>

#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/scene/scene_draw_list.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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
 * @brief Validates every description in one asset revision.
 * @param assets Complete asset collection to validate before planning.
 * @throws std::invalid_argument if any mesh, material, or relationship is invalid.
 */
void validateAssets(const RenderAssets& assets);

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
        validateAssets(assets);
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

void validateAssets(const RenderAssets& assets)
{
    for (const Mesh& mesh : assets.meshes())
    {
        if (mesh.vertices.empty())
        {
            throw std::invalid_argument("A prepared mesh must contain vertices");
        }
        if (mesh.indices.empty() || mesh.indices.size() % 3 != 0)
        {
            throw std::invalid_argument("A prepared mesh must contain complete indexed triangles");
        }
        if (mesh.indices.size() > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("A prepared mesh has too many indices for one draw");
        }
        if (std::ranges::any_of(mesh.indices, [&mesh](std::uint32_t index)
                                { return index >= mesh.vertices.size(); }))
        {
            throw std::invalid_argument("A mesh index refers beyond its vertex array");
        }
    }

    for (const Material& material : assets.materials())
    {
        const Color4 colour = material.baseColour;
        if (!std::isfinite(colour.r) || !std::isfinite(colour.g) || !std::isfinite(colour.b) ||
            !std::isfinite(colour.a))
        {
            throw std::invalid_argument("A material base colour must be finite");
        }
    }

    for (const RenderObject& renderObject : assets.renderObjects())
    {
        if (!renderObject.mesh.valid() || renderObject.mesh.value >= assets.meshes().size())
        {
            throw std::invalid_argument("A render object refers to a missing mesh");
        }
        if (!renderObject.material.valid() ||
            renderObject.material.value >= assets.materials().size())
        {
            throw std::invalid_argument("A render object refers to a missing material");
        }
    }
}

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
