#pragma once

#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/scene/scene_node_id.hpp>

namespace fire_engine
{
struct SceneContent;

namespace tutorial
{
/** @cond INTERNAL */

/** @brief Stable identities describing one caster node's changed dependency. */
struct ShadowCasterReplacement
{
    SceneNodeId node;              ///< Existing scene node redirected to the replacement.
    RenderObjectId originalObject; ///< Original description, retained in the asset catalog.
    RenderObjectId
        replacementObject; ///< Appended casting object with changed, untextured geometry.
};

/**
 * @brief Rebinds the unique casting scene node to an untextured, narrowed copy of its mesh.
 * @param content Receiver fixture whose other draws are already untextured.
 * @return Node identity and the original and replacement render-object IDs.
 * @throws std::logic_error if there is not exactly one casting node with a scene identity.
 * @throws std::out_of_range if a referenced object, caster mesh, or caster material is missing.
 *
 * Scales local mesh X coordinates by 0.75 without changing node transforms, hierarchy,
 * animation, or non-casters. Appends three descriptions; no existing asset is edited or
 * removed. The receiver fixture's next preparation therefore needs no images, even
 * though the original textured descriptions remain in the catalog. The caller must
 * prepare the changed dependencies before drawing; this helper performs no GPU work.
 */
[[nodiscard]] ShadowCasterReplacement replaceShadowCaster(SceneContent& content);

/** @endcond */
} // namespace tutorial
} // namespace fire_engine
