#include <fire_engine/graphics/detail/asset_validation.hpp>

#include <fire_engine/graphics/render_assets.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal functions --- */

void validateAssets(const RenderAssets& assets)
{
    for (const Mesh& mesh : assets.meshes())
    {
        if (mesh.vertices.empty())
        {
            throw std::invalid_argument("A prepared mesh must contain vertices");
        }
        if (mesh.indices.empty() || mesh.indices.size() % 3 != 0)
        {
            throw std::invalid_argument("A prepared mesh must contain complete indexed triangles");
        }
        if (mesh.indices.size() > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("A prepared mesh has too many indices for one draw");
        }
        if (std::ranges::any_of(mesh.indices, [&mesh](std::uint32_t index)
                                { return index >= mesh.vertices.size(); }))
        {
            throw std::invalid_argument("A mesh index refers beyond its vertex array");
        }
    }

    for (const Material& material : assets.materials())
    {
        const Color4 colour = material.baseColour;
        if (!std::isfinite(colour.r) || !std::isfinite(colour.g) || !std::isfinite(colour.b) ||
            !std::isfinite(colour.a))
        {
            throw std::invalid_argument("A material base colour must be finite");
        }
    }

    for (const RenderObject& renderObject : assets.renderObjects())
    {
        if (!renderObject.mesh.valid() || renderObject.mesh.value >= assets.meshes().size())
        {
            throw std::invalid_argument("A render object refers to a missing mesh");
        }
        if (!renderObject.material.valid() ||
            renderObject.material.value >= assets.materials().size())
        {
            throw std::invalid_argument("A render object refers to a missing material");
        }
    }
}
/** @endcond */
} // namespace fire_engine::detail
