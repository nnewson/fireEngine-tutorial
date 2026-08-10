#include <fire_engine/animation/detail/animation_validation.hpp>

#include <fire_engine/animation/animation.hpp>
#include <fire_engine/scene/animator.hpp>
#include <fire_engine/scene/scene.hpp>
#include <fire_engine/scene/scene_node.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <variant>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local function declarations --- */

/**
 * @brief Validates Animator bindings recursively from one scene node.
 * @param node Root of the subtree being inspected.
 * @param animations Reusable animations indexed by Animator components.
 */
void validateSubtreeBindings(const SceneNode& node, std::span<const Animation> animations);
/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Internal functions --- */

void validateAnimation(const Animation& animation)
{
    for (const AnimationChannel& channel : animation.channels)
    {
        if (channel.timestamps.empty() || channel.timestamps.size() != channel.values.size())
        {
            throw std::invalid_argument("An animation channel must contain matching samples");
        }
        if (!std::ranges::all_of(channel.timestamps,
                                 [](float timestamp) { return std::isfinite(timestamp); }) ||
            channel.timestamps.front() < 0.0f ||
            !std::ranges::is_sorted(channel.timestamps, std::ranges::less{}))
        {
            throw std::invalid_argument(
                "Animation timestamps must be finite, non-negative, and ordered");
        }
        if (std::ranges::adjacent_find(channel.timestamps) != channel.timestamps.end())
        {
            throw std::invalid_argument("Animation timestamps must be strictly increasing");
        }
        if (!std::ranges::all_of(channel.values, [](const Quaternion& value)
                                 { return value.normalized().has_value(); }))
        {
            throw std::invalid_argument("Animation rotations must be finite and non-zero");
        }
    }
}

void validateAnimationBindings(const Scene& scene, std::span<const Animation> animations)
{
    for (const Animation& animation : animations)
    {
        validateAnimation(animation);
    }
    for (const std::unique_ptr<SceneNode>& root : scene.roots())
    {
        validateSubtreeBindings(*root, animations);
    }
}
/** @endcond */

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

void validateSubtreeBindings(const SceneNode& node, std::span<const Animation> animations)
{
    const Animator* animator = std::get_if<Animator>(&node.component());
    if (animator != nullptr)
    {
        if (!animator->animation.valid() || animator->animation.value >= animations.size())
        {
            throw std::invalid_argument("An animator refers to a missing animation");
        }
        const Animation& animation = animations[animator->animation.value];
        if (!animator->channel.valid() || animator->channel.value >= animation.channels.size())
        {
            throw std::invalid_argument("An animator refers to a missing animation channel");
        }
        if (animator->targetPath != AnimationTargetPath::eRotation)
        {
            throw std::invalid_argument("An animator uses an unsupported target path");
        }
        if (!std::isfinite(animator->playbackTime) || animator->playbackTime < 0.0f)
        {
            throw std::invalid_argument("Animator playback time must be finite and non-negative");
        }
    }

    for (const std::unique_ptr<SceneNode>& child : node.children())
    {
        validateSubtreeBindings(*child, animations);
    }
}
/** @endcond */
} // namespace
} // namespace fire_engine::detail
