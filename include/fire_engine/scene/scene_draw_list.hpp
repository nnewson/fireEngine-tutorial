#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <fire_engine/graphics/draw_item.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/**
 * @brief Immutable view of current scene draws and their preparation-relevant identity.
 *
 * The dependency hash covers the ordered RenderObjectId sequence, including
 * duplicates, but deliberately excludes world transforms. Moving an instance
 * changes commands recorded for a frame without changing the GPU assets or
 * pipeline state required to draw it.
 *
 * The draw span remains valid until its backing SceneDrawListArena builds
 * another list or is destroyed. Copying this non-owning view does not extend
 * that lifetime; consumers must not retain it across another arena build.
 */
struct SceneDrawList
{
    std::span<const DrawItem> drawItems; ///< Current instances in depth-first scene order.
    std::size_t dependencyHash = 0;      ///< Hash of ordered render-object dependencies.
};

/* --- Classes --- */

/** @brief Owns reusable contiguous storage behind one immutable scene draw-list view. */
class SceneDrawListArena final
{
public:
    /** @brief Creates empty reusable draw-item storage. */
    SceneDrawListArena() = default;
    /** @brief Releases the draw-item storage after every view has expired. */
    ~SceneDrawListArena() = default;

    // Copying or moving storage would invalidate spans already handed to CPU consumers.
    SceneDrawListArena(const SceneDrawListArena&) = delete;
    SceneDrawListArena& operator=(const SceneDrawListArena&) = delete;
    SceneDrawListArena(SceneDrawListArena&&) = delete;
    SceneDrawListArena& operator=(SceneDrawListArena&&) = delete;

private:
    friend class Scene;

    std::vector<DrawItem> drawItems_; ///< Reused high-water storage for one serial snapshot.
};
} // namespace fire_engine
