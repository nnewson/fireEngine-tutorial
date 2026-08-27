#include <fire_engine/render/detail/compiled_resources.hpp>

#include <fire_engine/graphics/image_data.hpp>
#include <fire_engine/graphics/material.hpp>
#include <fire_engine/graphics/mesh.hpp>
#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/graphics/render_preparation.hpp>
#include <fire_engine/graphics/texture.hpp>
#include <fire_engine/graphics/vertex.hpp>
#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/buffer.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/frame_in_flight.hpp>
#include <fire_engine/render/detail/image.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

/* --- File-local classes --- */

/** @brief GPU buffers compiled from one CPU mesh description. */
class CompiledMesh final
{
public:
    /**
     * @brief Uploads one validated CPU mesh into vertex and index buffers.
     * @param allocator VMA owner used to create both buffers.
     * @param mesh Validated CPU geometry copied into the buffers.
     */
    CompiledMesh(const MemoryAllocator& allocator, const Mesh& mesh)
        : vertexBuffer_{allocator, mesh.vertices.size() * sizeof(Vertex),
                        vk::BufferUsageFlagBits::eVertexBuffer},
          indexBuffer_{allocator, mesh.indices.size() * sizeof(std::uint32_t),
                       vk::BufferUsageFlagBits::eIndexBuffer},
          indexCount_{static_cast<std::uint32_t>(mesh.indices.size())}
    {
        vertexBuffer_.write(std::as_bytes(std::span{mesh.vertices}));
        indexBuffer_.write(std::as_bytes(std::span{mesh.indices}));
    }

    /** @brief Returns the uploaded vertex buffer. @return Vertex-buffer allocation. */
    [[nodiscard]] const AllocatedBuffer& vertexBuffer() const noexcept
    {
        return vertexBuffer_;
    }

    /** @brief Returns the uploaded index buffer. @return Index-buffer allocation. */
    [[nodiscard]] const AllocatedBuffer& indexBuffer() const noexcept
    {
        return indexBuffer_;
    }

    /** @brief Returns the uploaded index count. @return Number consumed by drawIndexed. */
    [[nodiscard]] std::uint32_t indexCount() const noexcept
    {
        return indexCount_;
    }

private:
    AllocatedBuffer vertexBuffer_; ///< GPU buffer containing tightly packed vertices.
    AllocatedBuffer indexBuffer_;  ///< GPU buffer containing 32-bit triangle indices.
    std::uint32_t indexCount_;     ///< Number of indices consumed by drawIndexed.
};

/** @brief Device-local sampled image compiled from decoded RGBA8 pixels. */
class CompiledImage final
{
public:
    /**
     * @brief Allocates an image and creates its color view.
     * @param device Logical device that owns the image view.
     * @param allocator VMA owner used for the image allocation.
     * @param source Validated decoded image whose extent is retained.
     */
    CompiledImage(const Device& device, const MemoryAllocator& allocator, const ImageData& source)
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

    /** @brief Returns the allocated Vulkan image. @return Non-owning image handle. */
    [[nodiscard]] vk::Image image() const noexcept
    {
        return image_.handle();
    }

    /** @brief Returns the shader-visible color view. @return Owned image view. */
    [[nodiscard]] const vk::raii::ImageView& view() const noexcept
    {
        return view_;
    }

private:
    AllocatedImage image_;     ///< Device-local image and allocation.
    vk::raii::ImageView view_; ///< Single-mip color view into image_.
};

/** @brief Sampling state paired with one compiled image. */
class CompiledTexture final
{
public:
    /**
     * @brief Creates sampling state for a compiled texture description.
     * @param device Logical device that owns the sampler.
     * @param image Compiled image that must outlive this texture.
     * @param source Validated filtering and addressing description.
     */
    CompiledTexture(const Device& device, const CompiledImage& image, const Texture& source);

    /** @brief Returns the sampled image. @return Borrowed compiled image. */
    [[nodiscard]] const CompiledImage& image() const noexcept
    {
        return *image_;
    }

    /** @brief Returns the Vulkan sampler. @return Owned sampler. */
    [[nodiscard]] const vk::raii::Sampler& sampler() const noexcept
    {
        return sampler_;
    }

private:
    const CompiledImage* image_ = nullptr; ///< Borrowed image retained by owner ordering.
    vk::raii::Sampler sampler_;            ///< Filtering and addressing state.
};

/** @brief One decoded image and staging buffer awaiting a transfer command. */
struct PendingImageUpload
{
    std::unique_ptr<AllocatedBuffer> staging;   ///< Host-visible transfer source.
    const ImageData* source = nullptr;          ///< CPU pixels and copy extent.
    const CompiledImage* destination = nullptr; ///< Device-local transfer destination.
};

/** @brief Prepared lookup target retaining compiled-resource borrowers. */
struct CompiledRenderObject
{
    const CompiledMesh* mesh = nullptr;       ///< Shared compiled geometry.
    const CompiledTexture* texture = nullptr; ///< Sampled base-color texture.
    Color4 baseColor{};                       ///< Material factor pushed for each draw.
};

/** @brief Complete candidate ownership graph awaiting an ordered replacement. */
struct CompiledReplacement
{
    std::vector<std::unique_ptr<CompiledImage>> images;     ///< Dense ImageId lookup.
    std::vector<std::unique_ptr<CompiledTexture>> textures; ///< Dense TextureId lookup.
    std::unique_ptr<CompiledImage> fallbackImage;           ///< New persistent white image, if any.
    std::unique_ptr<CompiledTexture> fallbackTexture;       ///< Sampler for fallbackImage.
    std::vector<std::unique_ptr<CompiledMesh>> meshes;      ///< Dense MeshId lookup.
    std::vector<CompiledRenderObject> objects;              ///< Dense RenderObjectId lookup.
};

/* --- File-local function declarations --- */

[[nodiscard]] vk::Filter compileFilter(TextureFilter filter);
[[nodiscard]] vk::SamplerAddressMode compileWrap(TextureWrap wrap);
void uploadImages(const Device& device, FrameInFlight& frame,
                  std::span<const PendingImageUpload> uploads);
/** @endcond */
} // namespace

/* --- Private implementation classes --- */

/** @cond INTERNAL */
/** @brief Hidden ownership graph for prepared GPU resources. */
class CompiledResources::Impl final
{
public:
    /**
     * @brief Commits a complete replacement while preserving borrower-owner ordering.
     * @param replacement Candidate graph whose ownership moves into this instance.
     */
    void commitReplacement(CompiledReplacement replacement)
    {
        // Borrowers hold raw pointers into owners, so replace them first. Do not
        // collapse this into member-wise assignment: declaration order replaces
        // the owners first instead.
        objects = std::move(replacement.objects);
        meshes = std::move(replacement.meshes);
        if (replacement.fallbackTexture != nullptr)
        {
            fallbackTexture = std::move(replacement.fallbackTexture);
            fallbackImage = std::move(replacement.fallbackImage);
        }
        textures = std::move(replacement.textures);
        images = std::move(replacement.images);
    }

    std::vector<std::unique_ptr<CompiledImage>> images;     ///< Dense ImageId lookup.
    std::vector<std::unique_ptr<CompiledTexture>> textures; ///< Dense TextureId lookup.
    std::unique_ptr<CompiledImage> fallbackImage;           ///< Persistent one-pixel white image.
    std::unique_ptr<CompiledTexture> fallbackTexture;       ///< Sampler paired with fallbackImage.
    std::vector<std::unique_ptr<CompiledMesh>> meshes;      ///< Dense MeshId lookup.
    std::vector<CompiledRenderObject> objects;              ///< Dense RenderObjectId draw lookup.
};
/** @endcond */

/* --- Public member functions --- */

CompiledResources::CompiledResources()
    : implementation_{std::make_unique<Impl>()}
{
}

CompiledResources::~CompiledResources() = default;

void CompiledResources::replace(const Device& device, const MemoryAllocator& allocator,
                                FrameInFlight& frame, const RenderAssets& assets,
                                const RenderPreparationPlan& plan)
{
    const ImageData fallbackSource{
        .width = 1,
        .height = 1,
        .pixels = {255, 255, 255, 255},
    };
    std::unique_ptr<CompiledImage> newFallbackImage;
    std::unique_ptr<CompiledTexture> newFallbackTexture;

    std::vector<std::unique_ptr<CompiledImage>> images(assets.images().size());
    std::vector<PendingImageUpload> uploads;
    uploads.reserve(plan.images.size() + (implementation_->fallbackImage == nullptr ? 1 : 0));

    const auto stageImage = [&](const ImageData& source, const CompiledImage& destination)
    {
        auto staging = std::make_unique<AllocatedBuffer>(allocator, source.pixels.size(),
                                                         vk::BufferUsageFlagBits::eTransferSrc);
        staging->write(std::as_bytes(std::span{source.pixels}));
        uploads.push_back({
            .staging = std::move(staging),
            .source = &source,
            .destination = &destination,
        });
    };

    if (implementation_->fallbackImage == nullptr)
    {
        newFallbackImage = std::make_unique<CompiledImage>(device, allocator, fallbackSource);
        stageImage(fallbackSource, *newFallbackImage);
    }
    for (const ImageId imageId : plan.images)
    {
        const ImageData& source = assets.images()[imageId.value];
        images[imageId.value] = std::make_unique<CompiledImage>(device, allocator, source);
        stageImage(source, *images[imageId.value]);
    }
    if (!uploads.empty())
    {
        uploadImages(device, frame, uploads);
    }

    if (newFallbackImage != nullptr)
    {
        const Texture fallbackDescription{};
        newFallbackTexture =
            std::make_unique<CompiledTexture>(device, *newFallbackImage, fallbackDescription);
    }
    // The local owns the fallback only during its first preparation, before it
    // moves into the persistent member. Later replacements reuse that member.
    const CompiledTexture* const fallback = newFallbackTexture != nullptr
                                                ? newFallbackTexture.get()
                                                : implementation_->fallbackTexture.get();
    if (fallback == nullptr)
    {
        throw std::logic_error("Renderer fallback texture was not initialized");
    }

    std::vector<std::unique_ptr<CompiledTexture>> textures(assets.textures().size());
    for (const TextureId textureId : plan.textures)
    {
        const Texture& source = assets.textures()[textureId.value];
        textures[textureId.value] =
            std::make_unique<CompiledTexture>(device, *images[source.image.value], source);
    }

    std::vector<std::unique_ptr<CompiledMesh>> meshes(assets.meshes().size());
    for (const MeshId meshId : plan.meshes)
    {
        meshes[meshId.value] =
            std::make_unique<CompiledMesh>(allocator, assets.meshes()[meshId.value]);
    }

    std::vector<CompiledRenderObject> objects(assets.renderObjects().size());
    for (const PreparedRenderObject& object : plan.renderObjects)
    {
        const Material& material = assets.materials()[object.material.value];
        objects[object.id.value] = {
            .mesh = meshes[object.mesh.value].get(),
            .texture = material.baseColorTexture.has_value()
                           ? textures[material.baseColorTexture->value].get()
                           : fallback,
            .baseColor = material.baseColor,
        };
    }

    implementation_->commitReplacement({
        .images = std::move(images),
        .textures = std::move(textures),
        .fallbackImage = std::move(newFallbackImage),
        .fallbackTexture = std::move(newFallbackTexture),
        .meshes = std::move(meshes),
        .objects = std::move(objects),
    });
}

bool CompiledResources::contains(RenderObjectId id) const noexcept
{
    return id.valid() && id.value < implementation_->objects.size() &&
           implementation_->objects[id.value].mesh != nullptr &&
           implementation_->objects[id.value].texture != nullptr;
}

CompiledDraw CompiledResources::draw(RenderObjectId id) const
{
    if (!contains(id))
    {
        throw std::out_of_range("Render object is not part of the compiled plan");
    }
    const CompiledRenderObject& object = implementation_->objects[id.value];
    return {
        .vertexBuffer = object.mesh->vertexBuffer().handle(),
        .indexBuffer = object.mesh->indexBuffer().handle(),
        .indexCount = object.mesh->indexCount(),
        .sampler = *object.texture->sampler(),
        .imageView = *object.texture->image().view(),
        .baseColor = object.baseColor,
    };
}

namespace
{
/** @cond INTERNAL */
/* --- File-local class member functions --- */

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

/**
 * @brief Copies staged pixels into device-local images and makes them shader-readable.
 * @param device Device and graphics queue used for the transfer submission.
 * @param frame Reusable command buffer and fence; no work may be pending on them.
 * @param uploads Valid source, staging, and destination triples.
 * @throws vk::SystemError if command recording, submission, or waiting fails.
 *
 * This setup-time helper deliberately borrows the sole frame slot instead of
 * introducing a second command pool and fence. The caller must first quiesce
 * any earlier frame work; a dedicated upload context should replace this
 * arrangement before the renderer introduces multiple frames in flight.
 */
void uploadImages(const Device& device, FrameInFlight& frame,
                  std::span<const PendingImageUpload> uploads)
{
    frame.resetCommands();
    const vk::raii::CommandBuffer& commandBuffer = frame.commandBuffer();
    const vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBuffer.begin(beginInfo);

    for (const PendingImageUpload& upload : uploads)
    {
        const vk::ImageMemoryBarrier2 toTransfer{
            .srcStageMask = vk::PipelineStageFlagBits2::eNone,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = upload.destination->image(),
            .subresourceRange = kColorSubresourceRange,
        };
        const vk::DependencyInfo transferDependency{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toTransfer,
        };
        commandBuffer.pipelineBarrier2(transferDependency);

        const vk::BufferImageCopy copyRegion{
            .imageSubresource =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageExtent =
                {
                    .width = upload.source->width,
                    .height = upload.source->height,
                    .depth = 1,
                },
        };
        commandBuffer.copyBufferToImage(upload.staging->handle(), upload.destination->image(),
                                        vk::ImageLayout::eTransferDstOptimal, copyRegion);

        const vk::ImageMemoryBarrier2 toShaderRead{
            .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = upload.destination->image(),
            .subresourceRange = kColorSubresourceRange,
        };
        const vk::DependencyInfo shaderDependency{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toShaderRead,
        };
        commandBuffer.pipelineBarrier2(shaderDependency);
    }
    commandBuffer.end();

    const vk::CommandBufferSubmitInfo commandInfo{
        .commandBuffer = *commandBuffer,
    };
    const vk::SubmitInfo2 submitInfo{
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo,
    };
    device.logicalDevice().resetFences(*frame.frameFinished());
    device.graphicsQueue().submit2(submitInfo, *frame.frameFinished());
    const vk::Result result = device.logicalDevice().waitForFences(
        *frame.frameFinished(), vk::True, std::numeric_limits<std::uint64_t>::max());
    if (result != vk::Result::eSuccess)
    {
        throw vk::SystemError{vk::make_error_code(result), "Waiting for image upload completion"};
    }
}
/** @endcond */
} // namespace
} // namespace fire_engine::detail
