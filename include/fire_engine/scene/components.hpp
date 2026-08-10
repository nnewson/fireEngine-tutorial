#pragma once

#include <variant>

#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/scene/animator.hpp>

namespace fire_engine
{
/* --- Type aliases --- */

/**
 * @brief One optional behavior or renderable role attached to a scene node.
 *
 * std::monostate represents a transform-only hierarchy node. Animator nodes
 * will drive their transform once playback is introduced, while RenderObjectId
 * nodes emit draw items during traversal.
 */
using SceneComponent = std::variant<std::monostate, Animator, RenderObjectId>;
} // namespace fire_engine
