#include <fire_engine/graphics/render_assets.hpp>

#include <fire_engine/graphics/detail/render_assets_identity.hpp>

#include <atomic>
#include <cstdint>
#include <exception>
#include <optional>
#include <utility>

namespace fire_engine
{
namespace
{
/** @cond INTERNAL */
/* --- File-local function declarations --- */

/**
 * @brief Issues a process-unique catalog identity.
 * @return A nonzero identity never issued before.
 */
[[nodiscard]] RenderAssetsId newRenderAssetsId() noexcept;
/** @endcond */
} // namespace

/* --- Public member functions --- */

RenderAssets::RenderAssets() noexcept
    : id_{newRenderAssetsId()}
{
}

RenderAssets::RenderAssets(RenderAssets&& other) noexcept
    : meshes_{std::move(other.meshes_)},
      images_{std::move(other.images_)},
      textures_{std::move(other.textures_)},
      materials_{std::move(other.materials_)},
      renderObjects_{std::move(other.renderObjects_)},
      id_{other.id_},
      revision_{other.revision_}
{
    // The identity follows the content. The source must not keep it, or two
    // live catalogs could append different entries under one identity.
    other.restart();
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

    // This object's previous content and identity end here. It adopts the
    // source's identity with its content, so equal-looking IDs, revisions
    // and dependencies from the replaced content can never match it.
    id_ = other.id_;
    revision_ = other.revision_;
    other.restart();
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

RenderAssetsId RenderAssets::id() const noexcept
{
    return id_;
}

std::size_t RenderAssets::revision() const noexcept
{
    return revision_;
}

RenderAssetsStamp RenderAssets::stamp() const noexcept
{
    return {.catalog = id_, .revision = revision_};
}

/* --- Private member functions --- */

void RenderAssets::restart() noexcept
{
    // A moved-from vector is valid but unspecified; clearing makes the
    // restarted collection empty, matching its new identity at revision zero.
    meshes_.clear();
    images_.clear();
    textures_.clear();
    materials_.clear();
    renderObjects_.clear();
    id_ = newRenderAssetsId();
    revision_ = 0;
}

namespace detail
{
/** @cond INTERNAL */
/* --- Internal functions --- */

std::optional<std::uint64_t> issueRenderAssetsId(std::atomic<std::uint64_t>& counter) noexcept
{
    // Uniqueness needs only an atomic read-modify-write, not ordering with
    // other memory. Incrementing the largest value wraps to zero, which marks
    // the counter exhausted rather than restarting it.
    std::uint64_t issued = counter.load(std::memory_order_relaxed);
    do
    {
        if (issued == 0)
        {
            return std::nullopt;
        }
    } while (!counter.compare_exchange_weak(issued, issued + 1, std::memory_order_relaxed,
                                            std::memory_order_relaxed));
    return issued;
}
/** @endcond */
} // namespace detail

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

RenderAssetsId newRenderAssetsId() noexcept
{
    static std::atomic<std::uint64_t> nextId{1};
    const std::optional<std::uint64_t> issued = detail::issueRenderAssetsId(nextId);
    if (!issued.has_value())
    {
        // Issuing zero or a repeated identity would let unrelated content
        // match a prepared plan, so exhaustion ends the process instead.
        std::terminate();
    }
    return RenderAssetsId{.value = *issued};
}
/** @endcond */
} // namespace
} // namespace fire_engine
