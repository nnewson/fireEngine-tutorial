#pragma once

#include <optional>
#include <span>
#include <vector>

#include <fire_engine/graphics/draw_item.hpp>
#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class RenderAssets;

/* --- Classes --- */

/**
 * @brief Owned draws of one scene instant, stamped with the catalog they refer to.
 *
 * A snapshot holds draws and the identity and revision of the RenderAssets
 * catalog whose render-object IDs they use; it is not the complete scene. It
 * owns its draw storage, so building another snapshot never changes this one.
 * The storage can be reused only by ending the snapshot through
 * releaseStorage(), which makes the end of every draw view explicit at the
 * call site. The renderer reads a snapshot only during a call and keeps no
 * reference to it afterwards.
 */
class SceneSnapshot final
{
public:
    /** @brief Creates an empty snapshot with no catalog. */
    SceneSnapshot() noexcept = default;

    /**
     * @brief Takes ownership of draws whose render-object IDs refer to one catalog.
     * @param catalog Catalog whose identity and revision are recorded; not retained.
     * @param drawItems Draws moved into the snapshot. Callers must not write to this storage
     * through pointers kept from before the move.
     */
    SceneSnapshot(const RenderAssets& catalog, std::vector<DrawItem> drawItems);

    /** @brief Releases the owned draw storage. */
    ~SceneSnapshot() = default;

    // Copying would allocate a new draw buffer per frame; move or release instead.
    SceneSnapshot(const SceneSnapshot&) = delete;
    SceneSnapshot& operator=(const SceneSnapshot&) = delete;

    /**
     * @brief Moves the draws and catalog stamp from another snapshot.
     * @param other Snapshot left empty with no catalog.
     */
    SceneSnapshot(SceneSnapshot&& other) noexcept;

    /**
     * @brief Replaces this snapshot with another's draws and catalog stamp.
     * @param other Snapshot left empty with no catalog.
     * @return This snapshot.
     */
    SceneSnapshot& operator=(SceneSnapshot&& other) noexcept;

    /**
     * @brief Returns the draws in stored order.
     *
     * Scene::buildSnapshot() stores them in depth-first scene order; a snapshot
     * constructed from explicit draws keeps the order it was given.
     *
     * @return Read-only view valid while this snapshot holds its storage.
     */
    [[nodiscard]] std::span<const DrawItem> drawItems() const noexcept;

    /**
     * @brief Returns the catalog these draws refer to.
     * @return Identity and revision when constructed, including for zero draws; nullopt for a
     * default-constructed, moved-from, or released snapshot.
     */
    [[nodiscard]] std::optional<RenderAssetsStamp> catalog() const noexcept;

    /**
     * @brief Ends this snapshot and returns its draw storage for the next build.
     * @return The owned draw vector, contents included. This snapshot is left empty with no
     * catalog, and any view from drawItems() has ended.
     */
    [[nodiscard]] std::vector<DrawItem> releaseStorage() && noexcept;

private:
    std::vector<DrawItem> drawItems_;          ///< Owned draws in stored order.
    std::optional<RenderAssetsStamp> catalog_; ///< Catalog the draws' IDs refer to.
};
} // namespace fire_engine
