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
    for (const ImageData& image : assets.images())
    {
        if (image.width == 0 || image.height == 0)
        {
            throw std::invalid_argument("A decoded image must have a non-zero extent");
        }

        constexpr std::size_t kRgbaChannelCount = 4;
        const std::size_t width = image.width;
        const std::size_t height = image.height;
        if (width > std::numeric_limits<std::size_t>::max() / height / kRgbaChannelCount ||
            image.pixels.size() != width * height * kRgbaChannelCount)
        {
            throw std::invalid_argument("A decoded image must contain tightly packed RGBA8 pixels");
        }
    }

    for (const Texture& texture : assets.textures())
    {
        if (!texture.image.valid() || texture.image.value >= assets.images().size())
        {
            throw std::invalid_argument("A texture refers to a missing image");
        }
    }

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
        const Color4 color = material.baseColor;
        if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
            !std::isfinite(color.a))
        {
            throw std::invalid_argument("A material base color must be finite");
        }
        if (material.baseColorTexture.has_value() &&
            (!material.baseColorTexture->valid() ||
             material.baseColorTexture->value >= assets.textures().size()))
        {
            throw std::invalid_argument("A material refers to a missing base-color texture");
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
