#include "snapshot_acceptance.hpp"

#include <stdexcept>

namespace fire_engine::tutorial
{
/** @cond INTERNAL */
/* --- Internal functions --- */

RenderObjectId appendRenderObjectLike(RenderAssets& assets, RenderObjectId source)
{
    const RenderObject original = assets.renderObjects().at(source.value);
    return assets.addRenderObject(original);
}

RenderAssets duplicateCatalog(const RenderAssets& source)
{
    // Each kind has its own dense IDs, so adding kind by kind in ID order
    // reproduces every numeric ID; one insertion per entry reproduces the
    // revision of a catalog that was built by insertions alone.
    RenderAssets copy;
    for (const ImageData& image : source.images())
    {
        static_cast<void>(copy.addImage(image));
    }
    for (const Texture& texture : source.textures())
    {
        static_cast<void>(copy.addTexture(texture));
    }
    for (const Mesh& mesh : source.meshes())
    {
        static_cast<void>(copy.addMesh(mesh));
    }
    for (const Material& material : source.materials())
    {
        static_cast<void>(copy.addMaterial(material));
    }
    for (const RenderObject& renderObject : source.renderObjects())
    {
        static_cast<void>(copy.addRenderObject(renderObject));
    }
    if (copy.revision() != source.revision())
    {
        throw std::logic_error("A duplicated catalog does not reproduce its source's revision");
    }
    return copy;
}
/** @endcond */
} // namespace fire_engine::tutorial
