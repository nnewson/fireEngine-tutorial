#include <fire_engine/animation/animation_playback.hpp>

#include <fire_engine/animation/animation.hpp>
#include <fire_engine/scene/animator.hpp>
#include <fire_engine/scene/scene.hpp>
#include <fire_engine/scene/scene_node.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <variant>

namespace fire_engine
{
namespace
{
/** @cond INTERNAL */
/* --- File-local function declarations --- */

/**
 * @brief Samples one validated rotation channel at a playback time.
 * @param channel Channel containing ordered rotation samples.
 * @param playbackTime Time in seconds within the channel duration.
 * @return Normalized interpolated rotation.
 */
[[nodiscard]] Quaternion sampleRotation(const AnimationChannel& channel, float playbackTime);

/**
 * @brief Advances one validated animator and applies it to its owning node.
 * @param node Node whose optional Animator is advanced.
 * @param animations Target-independent animation data referenced by the Animator.
 * @param elapsedSeconds Non-negative time elapsed since the previous update.
 */
void advanceAnimator(SceneNode& node, std::span<const Animation> animations, float elapsedSeconds);

/**
 * @brief Advances Animator components recursively through one scene subtree.
 * @param node Root of the subtree being updated.
 * @param animations Target-independent animation data referenced by the subtree.
 * @param elapsedSeconds Non-negative time elapsed since the previous update.
 */
void advanceSubtree(SceneNode& node, std::span<const Animation> animations, float elapsedSeconds);
/** @endcond */
} // namespace

/* --- Public functions --- */

void advanceAnimations(Scene& scene, std::span<const Animation> animations, float elapsedSeconds)
{
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0f)
    {
        throw std::invalid_argument("Animation elapsed time must be finite and non-negative");
    }

    for (const std::unique_ptr<SceneNode>& root : scene.roots())
    {
        advanceSubtree(*root, animations, elapsedSeconds);
    }
}

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

[[nodiscard]] Quaternion sampleRotation(const AnimationChannel& channel, float playbackTime)
{
    const auto normalize = [](const Quaternion& value)
    {
        const auto normalized = value.normalized();
        if (!normalized.has_value())
        {
            throw std::logic_error("Animation playback encountered an invalid quaternion");
        }
        return *normalized;
    };

    if (playbackTime <= channel.timestamps.front())
    {
        return normalize(channel.values.front());
    }
    if (playbackTime >= channel.timestamps.back())
    {
        return normalize(channel.values.back());
    }

    const auto rightTimestamp = std::ranges::upper_bound(channel.timestamps, playbackTime);
    const std::size_t rightIndex =
        static_cast<std::size_t>(rightTimestamp - channel.timestamps.begin());
    const std::size_t leftIndex = rightIndex - 1;
    const float amount = (playbackTime - channel.timestamps[leftIndex]) /
                         (channel.timestamps[rightIndex] - channel.timestamps[leftIndex]);
    const auto sampled =
        channel.values[leftIndex].normalizedLerp(channel.values[rightIndex], amount);
    if (!sampled.has_value())
    {
        throw std::logic_error("Animation playback could not normalize an interpolated rotation");
    }
    return *sampled;
}

void advanceAnimator(SceneNode& node, std::span<const Animation> animations, float elapsedSeconds)
{
    const Animator* const component = std::get_if<Animator>(&node.component());
    if (component == nullptr)
    {
        return;
    }

    Animator animator = *component;
    if (!animator.animation.valid() || animator.animation.value >= animations.size())
    {
        throw std::logic_error("Animation playback encountered a missing animation");
    }
    const Animation& animation = animations[animator.animation.value];
    if (!animator.channel.valid() || animator.channel.value >= animation.channels.size())
    {
        throw std::logic_error("Animation playback encountered a missing channel");
    }
    if (animator.targetPath != AnimationTargetPath::eRotation)
    {
        throw std::logic_error("Animation playback encountered an unsupported target path");
    }

    const AnimationChannel& channel = animation.channels[animator.channel.value];
    if (channel.timestamps.empty() || channel.timestamps.size() != channel.values.size())
    {
        throw std::logic_error("Animation playback encountered an invalid channel");
    }

    // The addition and modulo use double, but playbackTime remains a float between frames.
    // Wrapping keeps that stored value small enough to advance; it does not remove gradual
    // float drift. Precise synchronization would need a double member or an absolute clock.
    const double advancedTime = static_cast<double>(animator.playbackTime) + elapsedSeconds;
    const float duration = channel.timestamps.back();
    if (animator.looping && duration > 0.0f)
    {
        animator.playbackTime = static_cast<float>(std::fmod(advancedTime, duration));
    }
    else
    {
        animator.playbackTime =
            static_cast<float>(std::min(advancedTime, static_cast<double>(duration)));
    }

    Transform transform = node.localTransform();
    transform.rotation = sampleRotation(channel, animator.playbackTime);
    node.localTransform(transform);
    node.component(animator);
}

void advanceSubtree(SceneNode& node, std::span<const Animation> animations, float elapsedSeconds)
{
    advanceAnimator(node, animations, elapsedSeconds);
    for (const std::unique_ptr<SceneNode>& child : node.children())
    {
        advanceSubtree(*child, animations, elapsedSeconds);
    }
}
/** @endcond */
} // namespace
} // namespace fire_engine
