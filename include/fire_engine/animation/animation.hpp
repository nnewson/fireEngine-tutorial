#pragma once

#include <string>
#include <vector>

#include <fire_engine/math/quaternion.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/**
 * @brief Target-independent linear quaternion samples reusable by scene animators.
 *
 * The initial supported subset is deliberately rotation-only. Translation,
 * scale, and weight channels will require value types appropriate to those
 * properties when a demonstrated use case brings them into scope.
 */
struct AnimationChannel
{
    std::vector<float> timestamps;  ///< Strictly increasing sample times in seconds.
    std::vector<Quaternion> values; ///< Rotation values corresponding to timestamps.
};

/** @brief Named collection of target-independent channels sharing one source animation. */
struct Animation
{
    std::string name;                       ///< Optional imported diagnostic name.
    std::vector<AnimationChannel> channels; ///< Reusable channels in stable index order.
};
} // namespace fire_engine
