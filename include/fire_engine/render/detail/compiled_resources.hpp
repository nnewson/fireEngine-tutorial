#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/render/detail/compiled_render_object.hpp>

namespace fire_engine::detail
{
struct CompiledResourceGraph;

/* --- Classes --- */

/** @brief Read-only render-object lookup borrowing one compiled-resource graph. */
class CompiledResourcesView final
{
public:
    /**
     * @brief Looks up the plain Vulkan bindings compiled for one render object.
     * @param id Render-object ID selected by the current scene.
     * @return Complete packet by value, or nullopt when the ID was not prepared.
     */
    [[nodiscard]] std::optional<CompiledRenderObject> find(RenderObjectId id) const noexcept;

    /**
     * @brief Returns the catalog the viewed graph was compiled from.
     * @return Identity and revision, or nullopt before any graph has been prepared.
     */
    [[nodiscard]] std::optional<RenderAssetsStamp> catalog() const noexcept;

private:
    friend class CompiledResources;

    /**
     * @brief Creates a restricted view of one stable render-object table.
     * @param objects Borrowed dense RenderObjectId lookup.
     * @param catalog Catalog stamp of the graph that owns the table.
     */
    CompiledResourcesView(std::span<const std::optional<CompiledRenderObject>> objects,
                          std::optional<RenderAssetsStamp> catalog) noexcept;

    std::span<const std::optional<CompiledRenderObject>> objects_; ///< Borrowed lookup capability.
    std::optional<RenderAssetsStamp> catalog_; ///< Catalog the viewed graph describes.
};

/**
 * @brief Owns the installed compiled-resource graph and the last submission that used it.
 *
 * The last use is the timeline serial of the latest successful frame
 * submission recorded against the installed graph, or none when no submission
 * has used it.
 */
class CompiledResources final
{
public:
    /** @brief Creates an empty compiled-resource collection. */
    CompiledResources();

    /** @brief Releases borrowers before their compiled buffer, image, and sampler owners. */
    ~CompiledResources();

    CompiledResources(const CompiledResources&) = delete;
    CompiledResources& operator=(const CompiledResources&) = delete;
    CompiledResources(CompiledResources&&) = delete;
    CompiledResources& operator=(CompiledResources&&) = delete;

    /**
     * @brief Returns a read-only lookup capability.
     * @return View valid until replacement or destruction.
     * @pre No replacement occurs while the returned view is in use.
     */
    [[nodiscard]] CompiledResourcesView view() const noexcept;

    /** @brief Returns the complete stable graph. @return Current compiler input and object owner.
     */
    [[nodiscard]] const CompiledResourceGraph& graph() const noexcept;

    /**
     * @brief Commits one complete compiler-produced ownership graph.
     * @param replacement Complete candidate whose borrowers refer only to its owners.
     * @pre No CompiledResourcesView or frame recording input still borrows the current graph.
     * @pre Every submission that used the current graph has completed.
     * @post No submission has used the installed graph.
     */
    void replace(std::unique_ptr<CompiledResourceGraph> replacement) noexcept;

    /**
     * @brief Records one successful frame submission against the installed graph.
     * @param serial Timeline serial published by that submission.
     * @pre @p serial exceeds the installed graph's previous last use.
     */
    void markUsed(std::uint64_t serial) noexcept;

    /**
     * @brief Returns the latest submission recorded against the installed graph.
     * @return Its timeline serial, or nullopt when no submission has used the graph.
     */
    [[nodiscard]] std::optional<std::uint64_t> lastUse() const noexcept;

private:
    std::unique_ptr<CompiledResourceGraph> graph_; ///< Complete stable GPU ownership graph.
    std::optional<std::uint64_t> lastUse_;         ///< Latest submission that used graph_.
};
} // namespace fire_engine::detail
