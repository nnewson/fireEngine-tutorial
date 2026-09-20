#pragma once

#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine
{
struct SceneContent;

namespace tutorial
{
/** @cond INTERNAL */

/**
 * @brief Appends the tutorial's procedural receive-only ground plane.
 * @param content Existing assets and scene hierarchy extended through their public APIs.
 * @return ID of the appended forward-visible render object, which does not cast shadows.
 */
[[nodiscard]] RenderObjectId addProceduralShadowReceiver(SceneContent& content);

/** @endcond */
} // namespace tutorial
} // namespace fire_engine
