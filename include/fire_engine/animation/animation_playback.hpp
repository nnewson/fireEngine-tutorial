#pragma once

#include <span>

namespace fire_engine
{
/* --- Forward declarations --- */

class Scene;
struct Animation;

/* --- Public functions --- */

/**
 * @brief Advances every Animator in a scene and applies its sampled local rotation.
 * @param scene Scene containing validated Animator components.
 * @param animations Target-independent animation data referenced by the scene.
 * @param elapsedSeconds Non-negative time elapsed since the previous update.
 * @throws std::invalid_argument if elapsedSeconds is negative or non-finite.
 * @throws std::logic_error if the previously validated animation bindings are broken.
 *
 * This function changes CPU-side local transforms only. Call
 * Scene::updateWorldTransforms() afterwards to resolve the hierarchy before drawing.
 */
void advanceAnimations(Scene& scene, std::span<const Animation> animations, float elapsedSeconds);
} // namespace fire_engine
