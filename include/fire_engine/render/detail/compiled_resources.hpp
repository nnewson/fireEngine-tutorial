#pragma once

#include <memory>
#include <optional>
#include <span>

#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/render/detail/compiled_draw.hpp>

namespace fire_engine::detail
{
struct CompiledResourceGraph;

/* --- Classes --- */

/** @brief Read-only draw lookup capability borrowing one compiled-resource graph. */
class CompiledResourcesView final
{
public:
    /**
     * @brief Looks up the plain Vulkan bindings compiled for one render object.
     * @param id Render-object ID selected by the current scene.
     * @return Complete packet by value, or nullopt when the ID was not prepared.
     */
    [[nodiscard]] std::optional<CompiledDraw> find(RenderObjectId id) const noexcept;

private:
    friend class CompiledResources;

    /**
     * @brief Creates a restricted view of one stable packet table.
     * @param draws Borrowed dense RenderObjectId lookup.
     */
    explicit CompiledResourcesView(std::span<const std::optional<CompiledDraw>> draws) noexcept;

    std::span<const std::optional<CompiledDraw>> draws_; ///< Borrowed packet-only capability.
};

/** @brief Owns the GPU resources compiled from the current render-preparation plan. */
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

    /** @brief Returns the complete stable graph. @return Current compiler input and draw owner. */
    [[nodiscard]] const CompiledResourceGraph& graph() const noexcept;

    /**
     * @brief Commits one complete compiler-produced ownership graph.
     * @param replacement Complete candidate whose borrowers refer only to its owners.
     * @pre No CompiledResourcesView or recording input still borrows the current graph.
     */
    void replace(std::unique_ptr<CompiledResourceGraph> replacement) noexcept;

private:
    std::unique_ptr<CompiledResourceGraph> graph_; ///< Complete stable GPU ownership graph.
};
} // namespace fire_engine::detail
