#pragma once

#include <span>

namespace fire_engine
{
class Scene;
struct Animation;

namespace detail
{
/** @cond INTERNAL */
/**
 * @brief Validates every target-independent timeline in one animation.
 * @param animation Animation description to validate.
 * @throws std::invalid_argument if a channel timeline or value is invalid.
 */
void validateAnimation(const Animation& animation);

/**
 * @brief Validates scene Animator components against externally owned animations.
 * @param scene Scene containing target-independent Animator bindings.
 * @param animations Reusable animations indexed by AnimationId.
 * @throws std::invalid_argument if an animation or channel reference is invalid.
 *
 * This is a setup-time composition check. It validates every curve and walks
 * the scene, so frame updates should consume an already validated binding set
 * rather than call this function repeatedly.
 */
void validateAnimationBindings(const Scene& scene, std::span<const Animation> animations);
/** @endcond */
} // namespace detail
} // namespace fire_engine
