#include <fire_engine/render/detail/pipeline_vertex_input.hpp>

#include <fire_engine/graphics/vertex.hpp>

#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

/** @brief Interleaved binding shared by every current graphics pipeline. */
constexpr vk::VertexInputBindingDescription kPositionColorTextureCoordinateBinding{
    .binding = 0,
    .stride = static_cast<std::uint32_t>(sizeof(Vertex)),
    .inputRate = vk::VertexInputRate::eVertex,
};

static_assert(std::is_standard_layout_v<Vertex>);
static_assert(sizeof(Vertex) == 9 * sizeof(float));
/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Internal functions --- */

const vk::VertexInputBindingDescription& compileVertexBinding(VertexLayoutKey vertexLayout)
{
    switch (vertexLayout)
    {
    case VertexLayoutKey::ePositionColorTextureCoordinate:
        return kPositionColorTextureCoordinateBinding;
    }
    throw std::invalid_argument("Pipeline description contains an unsupported vertex layout");
}
/** @endcond */
} // namespace fire_engine::detail
