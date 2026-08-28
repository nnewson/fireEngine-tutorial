#include <fire_engine/render/detail/compiled_resources.hpp>

#include <fire_engine/graphics/image_data.hpp>
#include <fire_engine/graphics/texture.hpp>
#include <fire_engine/graphics/vertex.hpp>
#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/compiled_resource_graph.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local function declarations --- */

[[nodiscard]] vk::Filter compileFilter(TextureFilter filter);
[[nodiscard]] vk::SamplerAddressMode compileWrap(TextureWrap wrap);
/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Internal member functions --- */

CompiledMesh::CompiledMesh(const MemoryAllocator& allocator, const Mesh& mesh)
    : vertexBuffer_{allocator, mesh.vertices.size() * sizeof(Vertex),
                    vk::BufferUsageFlagBits::eVertexBuffer},
      indexBuffer_{allocator, mesh.indices.size() * sizeof(std::uint32_t),
                   vk::BufferUsageFlagBits::eIndexBuffer},
      indexCount_{static_cast<std::uint32_t>(mesh.indices.size())},
      vertexLayout_{mesh.vertexLayout}
{
    vertexBuffer_.write(std::as_bytes(std::span{mesh.vertices}));
    indexBuffer_.write(std::as_bytes(std::span{mesh.indices}));
}

const AllocatedBuffer& CompiledMesh::vertexBuffer() const noexcept
{
    return vertexBuffer_;
}

const AllocatedBuffer& CompiledMesh::indexBuffer() const noexcept
{
    return indexBuffer_;
}

std::uint32_t CompiledMesh::indexCount() const noexcept
{
    return indexCount_;
}

VertexLayoutKey CompiledMesh::vertexLayout() const noexcept
{
    return vertexLayout_;
}

CompiledImage::CompiledImage(const Device& device, const MemoryAllocator& allocator,
                             const ImageData& source)
    : image_{allocator, source.width, source.height, vk::Format::eR8G8B8A8Srgb,
             vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled},
      view_{device.logicalDevice(), vk::ImageViewCreateInfo{
                                        .image = image_.handle(),
                                        .viewType = vk::ImageViewType::e2D,
                                        .format = vk::Format::eR8G8B8A8Srgb,
                                        .subresourceRange = kColorSubresourceRange,
                                    }}
{
}

vk::Image CompiledImage::image() const noexcept
{
    return image_.handle();
}

const vk::raii::ImageView& CompiledImage::view() const noexcept
{
    return view_;
}

CompiledTexture::CompiledTexture(const Device& device, const CompiledImage& image,
                                 const Texture& source)
    : image_{&image},
      sampler_{device.logicalDevice(), vk::SamplerCreateInfo{
                                           .magFilter = compileFilter(source.magFilter),
                                           .minFilter = compileFilter(source.minFilter),
                                           .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                           .addressModeU = compileWrap(source.wrapU),
                                           .addressModeV = compileWrap(source.wrapV),
                                           .addressModeW = vk::SamplerAddressMode::eRepeat,
                                           .minLod = 0.0f,
                                           .maxLod = 0.0f,
                                       }}
{
}

const CompiledImage& CompiledTexture::image() const noexcept
{
    return *image_;
}

const vk::raii::Sampler& CompiledTexture::sampler() const noexcept
{
    return sampler_;
}
/** @endcond */

/* --- Public member functions --- */

CompiledResources::CompiledResources()
    : graph_{std::make_unique<CompiledResourceGraph>()}
{
}

CompiledResources::~CompiledResources() = default;

bool CompiledResources::contains(RenderObjectId id) const noexcept
{
    return id.valid() && id.value < graph_->objects.size() &&
           graph_->objects[id.value].mesh != nullptr &&
           graph_->objects[id.value].texture != nullptr;
}

CompiledDraw CompiledResources::draw(RenderObjectId id) const
{
    if (!contains(id))
    {
        throw std::out_of_range("Render object is not part of the compiled plan");
    }
    const CompiledRenderObject& object = graph_->objects[id.value];
    return {
        .vertexBuffer = object.mesh->vertexBuffer().handle(),
        .indexBuffer = object.mesh->indexBuffer().handle(),
        .indexCount = object.mesh->indexCount(),
        .sampler = *object.texture->sampler(),
        .imageView = *object.texture->image().view(),
        .baseColor = object.baseColor,
        .vertexLayout = object.vertexLayout,
    };
}

const CompiledResourceGraph& CompiledResources::graph() const noexcept
{
    return *graph_;
}

void CompiledResources::replace(std::unique_ptr<CompiledResourceGraph> replacement) noexcept
{
    graph_ = std::move(replacement);
}

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

/**
 * @brief Converts a Vulkan-free texture filter into Vulkan sampling state.
 * @param filter Source filtering behavior.
 * @return Matching Vulkan filter.
 */
[[nodiscard]] vk::Filter compileFilter(TextureFilter filter)
{
    switch (filter)
    {
    case TextureFilter::eNearest:
        return vk::Filter::eNearest;
    case TextureFilter::eLinear:
        return vk::Filter::eLinear;
    }
    throw std::logic_error("Validated texture contains an unknown filtering mode");
}

/**
 * @brief Converts a Vulkan-free wrapping mode into Vulkan sampling state.
 * @param wrap Source coordinate-addressing behavior.
 * @return Matching Vulkan address mode.
 */
[[nodiscard]] vk::SamplerAddressMode compileWrap(TextureWrap wrap)
{
    switch (wrap)
    {
    case TextureWrap::eRepeat:
        return vk::SamplerAddressMode::eRepeat;
    case TextureWrap::eMirroredRepeat:
        return vk::SamplerAddressMode::eMirroredRepeat;
    case TextureWrap::eClampToEdge:
        return vk::SamplerAddressMode::eClampToEdge;
    }
    throw std::logic_error("Validated texture contains an unknown wrapping mode");
}
/** @endcond */
} // namespace
} // namespace fire_engine::detail
