#pragma once

#include <array>

namespace fire_engine
{
/* --- POD structs --- */

/**
 * @brief Vertex attributes consumed by the tutorial graphics pipeline.
 *
 * Renderer uploads instances of this standard-layout type, and Pipeline uses
 * the same type to describe their binding stride and attribute offsets.
 */
struct Vertex
{
    std::array<float, 2> position; ///< Clip-space position supplied at shader location zero.
    std::array<float, 3> color;    ///< Linear RGB color supplied at shader location one.
};
} // namespace fire_engine
