#pragma once

#include <cstdint>

#include <fire_engine/animation/animation_ids.hpp>

namespace fire_engine
{
/* --- Enums --- */

/** @brief Local transform component driven by an Animator binding. */
enum class AnimationTargetPath : std::uint8_t
{
    eRotation, ///< Replace the node's local rotation.
};

/* --- POD structs --- */

/** @brief Scene behavior binding one reusable animation channel to its owning node. */
struct Animator
{
    AnimationId animation;      ///< Animation containing the reusable channel.
    AnimationChannelId channel; ///< Animation-local channel index.
    AnimationTargetPath targetPath = AnimationTargetPath::eRotation; ///< Driven property.
    float playbackTime = 0.0f; ///< Independent runtime position in seconds.
    bool looping = true;       ///< Whether playback wraps at the animation duration.
};
} // namespace fire_engine
