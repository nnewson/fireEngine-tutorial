#pragma once

#include <cstddef>
#include <vector>

#include <fire_engine/graphics/draw_item.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/**
 * @brief Current scene draws and their preparation-relevant identity.
 *
 * The dependency hash covers the ordered RenderObjectId sequence, including
 * duplicates, but deliberately excludes world transforms. Moving an instance
 * changes commands recorded for a frame without changing the GPU assets or
 * pipeline state required to draw it.
 */
struct SceneDrawList
{
    std::vector<DrawItem> drawItems; ///< Current instances in depth-first scene order.
    std::size_t dependencyHash = 0;  ///< Hash of the ordered render-object dependencies.
};
} // namespace fire_engine
