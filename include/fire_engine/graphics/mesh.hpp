#pragma once

#include <cstdint>
#include <vector>

#include <fire_engine/graphics/pipeline_description.hpp>
#include <fire_engine/graphics/vertex.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief CPU-side indexed triangle mesh with no Vulkan ownership. */
struct Mesh
{
    std::vector<Vertex> vertices;       ///< Vertex attributes in binding order.
    std::vector<std::uint32_t> indices; ///< Triangle-list vertex indices.
    VertexLayoutKey vertexLayout =
        VertexLayoutKey::ePositionColorTextureCoordinate; ///< Vertex member sequence.
};
} // namespace fire_engine
