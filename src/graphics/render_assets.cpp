#include <fire_engine/graphics/render_assets.hpp>

#include <utility>

namespace fire_engine
{
/* --- Public member functions --- */

RenderAssets::RenderAssets(RenderAssets&& other) noexcept
    : meshes_{std::move(other.meshes_)},
      images_{std::move(other.images_)},
      textures_{std::move(other.textures_)},
      materials_{std::move(other.materials_)},
      renderObjects_{std::move(other.renderObjects_)},
      revision_{other.revision_}
{
    // Moving also mutates the source collection. Its revision must change in
    // case a RenderPreparation cache was previously built from that object.
    ++other.revision_;
}

RenderAssets& RenderAssets::operator=(RenderAssets&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    meshes_ = std::move(other.meshes_);
    images_ = std::move(other.images_);
    textures_ = std::move(other.textures_);
    materials_ = std::move(other.materials_);
    renderObjects_ = std::move(other.renderObjects_);

    // This object retains its identity, so increment its existing revision
    // rather than adopting a value that might equal the previously cached one.
    ++revision_;
    ++other.revision_;
    return *this;
}

MeshId RenderAssets::addMesh(Mesh mesh)
{
    const MeshId id{.value = meshes_.size()};
    meshes_.push_back(std::move(mesh));
    ++revision_;
    return id;
}

ImageId RenderAssets::addImage(ImageData image)
{
    const ImageId id{.value = images_.size()};
    images_.push_back(std::move(image));
    ++revision_;
    return id;
}

TextureId RenderAssets::addTexture(Texture texture)
{
    const TextureId id{.value = textures_.size()};
    textures_.push_back(texture);
    ++revision_;
    return id;
}

MaterialId RenderAssets::addMaterial(Material material)
{
    const MaterialId id{.value = materials_.size()};
    materials_.push_back(material);
    ++revision_;
    return id;
}

RenderObjectId RenderAssets::addRenderObject(RenderObject renderObject)
{
    const RenderObjectId id{.value = renderObjects_.size()};
    renderObjects_.push_back(renderObject);
    ++revision_;
    return id;
}

const std::vector<Mesh>& RenderAssets::meshes() const noexcept
{
    return meshes_;
}

const std::vector<ImageData>& RenderAssets::images() const noexcept
{
    return images_;
}

const std::vector<Texture>& RenderAssets::textures() const noexcept
{
    return textures_;
}

const std::vector<Material>& RenderAssets::materials() const noexcept
{
    return materials_;
}

const std::vector<RenderObject>& RenderAssets::renderObjects() const noexcept
{
    return renderObjects_;
}

std::size_t RenderAssets::revision() const noexcept
{
    return revision_;
}
} // namespace fire_engine
