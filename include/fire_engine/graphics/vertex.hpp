#pragma once

#include <fire_engine/graphics/color4.hpp>
#include <fire_engine/math/vec3.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Vulkan-free vertex attributes consumed by the tutorial shader. */
struct Vertex
{
    Vec3 position; ///< Object-space position.
    Color4 color;  ///< Linear RGBA vertex color.
};
} // namespace fire_engine
