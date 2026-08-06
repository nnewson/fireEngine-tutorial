#pragma once

#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief CPU-side relationship between one reusable mesh and material. */
struct RenderObject
{
    MeshId mesh;         ///< Mesh compiled into vertex and index buffers.
    MaterialId material; ///< Material selecting pipeline state and draw data.
};
} // namespace fire_engine
