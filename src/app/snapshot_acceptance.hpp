#pragma once

#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine::tutorial
{
/** @cond INTERNAL */

/**
 * @brief Appends a render object that reuses an existing object's mesh and material.
 * @param assets Catalog to extend; no existing entry is edited.
 * @param source Object whose mesh, material, and shadow participation are reused.
 * @return ID of the appended object. Preparing it needs no new mesh or image.
 * @throws std::out_of_range if @p source is not in the catalog.
 */
[[nodiscard]] RenderObjectId appendRenderObjectLike(RenderAssets& assets, RenderObjectId source);

/**
 * @brief Builds a separate catalog by adding every entry of another in ID order.
 * @param source Catalog whose entries are copied.
 * @return Catalog with the same entries, numeric IDs, and revision, but its own identity.
 * @throws std::logic_error if the copy's revision differs from the source's.
 */
[[nodiscard]] RenderAssets duplicateCatalog(const RenderAssets& source);

/** @endcond */
} // namespace fire_engine::tutorial
