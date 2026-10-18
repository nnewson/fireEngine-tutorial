#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <fire_engine/graphics/pipeline_description.hpp>
#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class RenderAssets;
class SceneSnapshot;

/* --- POD structs --- */

/** @brief Validated mesh/material relationship ready for GPU compilation. */
struct PreparedRenderObject
{
    RenderObjectId id;            ///< Dense object ID retained for renderer lookup.
    MeshId mesh;                  ///< Validated source mesh.
    MaterialId material;          ///< Validated source material.
    bool castsShadow;             ///< Directional-shadow participation retained for compilation.
    VertexLayoutKey vertexLayout; ///< Mesh/pipeline compatibility proved by preparation.
};

/** @brief Vulkan-free compilation plan for the current visible asset subset. */
struct RenderPreparationPlan
{
    std::vector<MeshId> meshes;        ///< Distinct required meshes in stable ID order.
    std::vector<ImageId> images;       ///< Distinct required decoded images in stable ID order.
    std::vector<TextureId> textures;   ///< Distinct required textures in stable ID order.
    std::vector<MaterialId> materials; ///< Distinct required materials in stable ID order.
    std::vector<PreparedRenderObject> renderObjects; ///< Required relationships by ID.
    PipelineDescription pipeline; ///< Pipeline requirements proved compatible with every draw.
    RenderAssetsStamp assets;     ///< Catalog identity and revision this plan was built from.
};

/* --- Classes --- */

/**
 * @brief Validates render descriptions and caches compilation of a scene snapshot's draws.
 *
 * Asset validation is repeated only when the catalog's identity or revision
 * changes. A cached plan is reused only for the exact catalog identity and
 * revision it was built from, the same pipeline, and the same ordered
 * RenderObjectId dependencies. The two are keyed separately because a build
 * can validate a catalog and then reject its draws, leaving the cached plan
 * from an earlier catalog. World transforms never invalidate the plan because
 * they are consumed later during command recording. The ordered dependencies
 * are copied, so no reference to a snapshot outlives build().
 *
 * Catalog identities follow content and are never reused, so a catalog
 * destroyed and replaced at the same address cannot match an earlier plan.
 */
class RenderPreparation final
{
public:
    RenderPreparation() = default;
    ~RenderPreparation() = default;

    RenderPreparation(const RenderPreparation&) = delete;
    RenderPreparation& operator=(const RenderPreparation&) = delete;
    /** @brief Moves the cached validation and plan state from another compiler. */
    RenderPreparation(RenderPreparation&&) noexcept = default;
    /** @brief Replaces this compiler by moving another. @return This compiler. */
    RenderPreparation& operator=(RenderPreparation&&) noexcept = default;

    /**
     * @brief Returns a cached plan or compiles one for the supplied dependencies.
     * @param assets Complete catalog of Vulkan-free render descriptions.
     * @param snapshot Current scene draws, read only during this call.
     * @param pipeline Vulkan-free requirements selected for the compiled draw packets.
     * @return Reference valid until the next build() call that changes the plan.
     * @throws std::invalid_argument if an asset or draw dependency is invalid.
     */
    [[nodiscard]] const RenderPreparationPlan&
    build(const RenderAssets& assets, const SceneSnapshot& snapshot, PipelineDescription pipeline);

    /**
     * @brief Returns how many distinct plans this compiler has produced.
     * @return Counter incremented only when build() cannot reuse its cached plan.
     */
    [[nodiscard]] std::size_t generation() const noexcept;

private:
    std::optional<RenderAssetsStamp> validatedAssets_;  ///< Last fully validated catalog state.
    std::vector<RenderObjectId> cachedDependencies_;    ///< Exact ordered draw dependencies.
    std::optional<PipelineDescription> cachedPipeline_; ///< Pipeline requirement in the plan key.
    std::optional<RenderPreparationPlan> cachedPlan_;   ///< Most recently compiled subset.
    std::size_t generation_ = 0; ///< Number of cache misses compiled by build().
};
} // namespace fire_engine
