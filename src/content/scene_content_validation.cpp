#include <fire_engine/content/detail/scene_content_validation.hpp>

#include <fire_engine/animation/detail/animation_validation.hpp>
#include <fire_engine/content/scene_content.hpp>
#include <fire_engine/graphics/detail/asset_validation.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Functions --- */

void validateSceneContent(const SceneContent& content)
{
    validateAssets(content.assets);
    validateAnimationBindings(content.scene, content.animations);
}
/** @endcond */
} // namespace fire_engine::detail
