#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class RenderAssets;
struct SceneDrawList;

/* --- POD structs --- */

/** @brief Validated mesh/material relationship ready for GPU compilation. */
struct PreparedRenderObject
{
    RenderObjectId id;   ///< Dense object ID retained for renderer lookup.
    MeshId mesh;         ///< Validated source mesh.
    MaterialId material; ///< Validated source material.
};

/** @brief Vulkan-free compilation plan for the current visible asset subset. */
struct RenderPreparationPlan
{
    std::vector<MeshId> meshes;        ///< Distinct required meshes in stable ID order.
    std::vector<ImageId> images;       ///< Distinct required decoded images in stable ID order.
    std::vector<TextureId> textures;   ///< Distinct required textures in stable ID order.
    std::vector<MaterialId> materials; ///< Distinct required materials in stable ID order.
    std::vector<PreparedRenderObject> renderObjects; ///< Required relationships by ID.
    std::size_t assetRevision = 0;                   ///< Asset revision represented by this plan.
    std::size_t dependencyHash = 0; ///< Transform-independent draw dependency hash.
};

/* --- Classes --- */

/**
 * @brief Validates render descriptions and caches compilation of a scene draw list.
 *
 * Asset validation is repeated only when the RenderAssets instance or revision
 * changes. Plan compilation is repeated only when that asset state or the exact
 * ordered RenderObjectId dependencies change. World transforms never invalidate
 * the plan because they are consumed later during command recording.
 *
 * Collection identity is represented by its address and revision. As with any
 * address-based cache, destroying a collection and constructing another at the
 * same address with the same revision requires a fresh RenderPreparation.
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
     * @param drawList Current scene instances and dependency hash.
     * @return Reference valid until the next build() call that changes the plan.
     * @throws std::invalid_argument if an asset or draw dependency is invalid.
     */
    [[nodiscard]] const RenderPreparationPlan& build(const RenderAssets& assets,
                                                     const SceneDrawList& drawList);

    /**
     * @brief Returns how many distinct plans this compiler has produced.
     * @return Counter incremented only when build() cannot reuse its cached plan.
     */
    [[nodiscard]] std::size_t generation() const noexcept;

private:
    const RenderAssets* validatedAssets_ = nullptr;   ///< Collection associated with validation.
    std::optional<std::size_t> validatedRevision_;    ///< Last fully validated asset revision.
    std::vector<RenderObjectId> cachedDependencies_;  ///< Exact collision-proof plan key.
    std::optional<RenderPreparationPlan> cachedPlan_; ///< Most recently compiled subset.
    std::size_t generation_ = 0; ///< Number of cache misses compiled by build().
};
} // namespace fire_engine
