#pragma once

#include <array>

namespace fire_engine
{
/* --- POD structs --- */

/**
 * @brief Vertex attributes consumed by the tutorial graphics pipeline.
 *
 * The first buffer milestone will upload instances of this standard-layout
 * type. The pipeline can define the interface before any buffer is allocated.
 */
struct Vertex
{
    std::array<float, 2> position; ///< Clip-space position supplied at shader location zero.
    std::array<float, 3> color;    ///< Linear RGB color supplied at shader location one.
};
} // namespace fire_engine
