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

    /**
     * @brief Whether this object participates in directional-shadow recording.
     *
     * This policy does not affect forward visibility. A second independently variable
     * pass-participation property is the trigger to move participation into a dedicated
     * scene-instance or pass-policy concept rather than adding another boolean here.
     */
    bool castsShadow = true;
};
} // namespace fire_engine
