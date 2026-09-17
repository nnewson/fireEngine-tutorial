#pragma once

#include <fire_engine/render/frame_description.hpp>

namespace fire_engine::tutorial
{
/** @cond INTERNAL */

/**
 * @brief Returns the fixed frame policy used by AnimatedCube and synthetic scenarios.
 * @return Immutable description with the original tutorial camera and registered shadow view.
 */
[[nodiscard]] const FrameDescription& animatedCubeFrameDescription() noexcept;

/**
 * @brief Returns the fixed frame policy used by the shadow demonstration.
 * @return Immutable description with the demonstration camera and registered shadow view.
 */
[[nodiscard]] const FrameDescription& shadowDemonstrationFrameDescription() noexcept;

/** @endcond */
} // namespace fire_engine::tutorial
