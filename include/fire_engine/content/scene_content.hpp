#pragma once

#include <vector>

#include <fire_engine/animation/animation.hpp>
#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/scene/scene.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/**
 * @brief Format-neutral render descriptions, hierarchy, and animations for one scene.
 *
 * Procedural builders, glTF importers, and future cached formats can all produce
 * this same composition without making its consumers depend on the source format.
 */
struct SceneContent
{
    RenderAssets assets;               ///< Render descriptions referenced by the scene.
    Scene scene;                       ///< Transform hierarchy and component bindings.
    std::vector<Animation> animations; ///< Target-independent animation curve data.
};
} // namespace fire_engine
