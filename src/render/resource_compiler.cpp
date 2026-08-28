#include <fire_engine/render/detail/resource_compiler.hpp>

#include <fire_engine/graphics/image_data.hpp>
#include <fire_engine/graphics/material.hpp>
#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/graphics/render_preparation.hpp>
#include <fire_engine/graphics/texture.hpp>
#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/buffer.hpp>
#include <fire_engine/render/detail/compiled_resource_graph.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>

#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local classes --- */

/** @brief One decoded image and staging buffer awaiting a transfer command. */
struct PendingImageUpload
{
    std::unique_ptr<AllocatedBuffer> staging;   ///< Host-visible transfer source.
    const ImageData* source = nullptr;          ///< CPU pixels and copy extent.
    const CompiledImage* destination = nullptr; ///< Device-local transfer destination.
};

/* --- File-local function declarations --- */

void recordImageUploads(const vk::raii::CommandBuffer& commandBuffer,
                        std::span<const PendingImageUpload> uploads);
/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Internal member functions --- */

ResourceCompiler::ResourceCompiler(const Device& device, const MemoryAllocator& allocator)
    : device_{&device},
      allocator_{&allocator}
{
    const vk::CommandPoolCreateInfo commandPoolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = device.graphicsQueueFamily(),
    };
    commandPool_ = vk::raii::CommandPool{device.logicalDevice(), commandPoolInfo};

    const vk::CommandBufferAllocateInfo commandBufferInfo{
        .commandPool = *commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    commandBuffers_ = vk::raii::CommandBuffers{device.logicalDevice(), commandBufferInfo};

    const vk::FenceCreateInfo fenceInfo{
        .flags = vk::FenceCreateFlagBits::eSignaled,
    };
    uploadFinished_ = vk::raii::Fence{device.logicalDevice(), fenceInfo};
}

std::unique_ptr<CompiledResourceGraph>
ResourceCompiler::compile(const RenderAssets& assets, const RenderPreparationPlan& plan,
                          const CompiledResourceGraph& previous)
{
    const ImageData fallbackSource{
        .width = 1,
        .height = 1,
        .pixels = {255, 255, 255, 255},
    };
    auto candidate = std::make_unique<CompiledResourceGraph>();
    candidate->images.resize(assets.images().size());
    candidate->textures.resize(assets.textures().size());
    candidate->meshes.resize(assets.meshes().size());
    candidate->objects.resize(assets.renderObjects().size());

    std::vector<PendingImageUpload> uploads;
    uploads.reserve(plan.images.size() + 1);
    const auto stageImage = [&](const ImageData& source, const CompiledImage& image)
    {
        auto staging = std::make_unique<AllocatedBuffer>(*allocator_, source.pixels.size(),
                                                         vk::BufferUsageFlagBits::eTransferSrc);
        staging->write(std::as_bytes(std::span{source.pixels}));
        uploads.push_back({
            .staging = std::move(staging),
            .source = &source,
            .destination = &image,
        });
    };

    if (previous.fallbackImage != nullptr)
    {
        candidate->fallbackImage = previous.fallbackImage;
        candidate->fallbackTexture = previous.fallbackTexture;
    }
    else
    {
        candidate->fallbackImage =
            std::make_shared<CompiledImage>(*device_, *allocator_, fallbackSource);
        stageImage(fallbackSource, *candidate->fallbackImage);
    }
    for (const ImageId imageId : plan.images)
    {
        const ImageData& source = assets.images()[imageId.value];
        candidate->images[imageId.value] =
            std::make_unique<CompiledImage>(*device_, *allocator_, source);
        stageImage(source, *candidate->images[imageId.value]);
    }

    if (!uploads.empty())
    {
        commandPool_.reset();
        const vk::raii::CommandBuffer& commandBuffer = commandBuffers_.front();
        const vk::CommandBufferBeginInfo beginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
        };
        commandBuffer.begin(beginInfo);
        recordImageUploads(commandBuffer, uploads);
        commandBuffer.end();

        const vk::CommandBufferSubmitInfo commandInfo{
            .commandBuffer = *commandBuffer,
        };
        const vk::SubmitInfo2 submitInfo{
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
        };
        device_->logicalDevice().resetFences(*uploadFinished_);
        device_->graphicsQueue().submit2(submitInfo, *uploadFinished_);
        const vk::Result uploadResult = device_->logicalDevice().waitForFences(
            *uploadFinished_, vk::True, std::numeric_limits<std::uint64_t>::max());
        if (uploadResult != vk::Result::eSuccess)
        {
            throw vk::SystemError{vk::make_error_code(uploadResult),
                                  "Waiting for image upload completion"};
        }
    }

    if (candidate->fallbackTexture == nullptr)
    {
        const Texture fallbackDescription{};
        candidate->fallbackTexture = std::make_shared<CompiledTexture>(
            *device_, *candidate->fallbackImage, fallbackDescription);
    }
    for (const TextureId textureId : plan.textures)
    {
        const Texture& source = assets.textures()[textureId.value];
        candidate->textures[textureId.value] = std::make_unique<CompiledTexture>(
            *device_, *candidate->images[source.image.value], source);
    }
    for (const MeshId meshId : plan.meshes)
    {
        candidate->meshes[meshId.value] =
            std::make_unique<CompiledMesh>(*allocator_, assets.meshes()[meshId.value]);
    }
    for (const PreparedRenderObject& object : plan.renderObjects)
    {
        const Material& material = assets.materials()[object.material.value];
        const CompiledMesh& mesh = *candidate->meshes[object.mesh.value];
        assert(mesh.vertexLayout() == object.vertexLayout);
        assert(object.vertexLayout == plan.pipeline.vertexLayout);
        candidate->objects[object.id.value] = {
            .mesh = &mesh,
            .texture = material.baseColorTexture.has_value()
                           ? candidate->textures[material.baseColorTexture->value].get()
                           : candidate->fallbackTexture.get(),
            .baseColor = material.baseColor,
            .vertexLayout = object.vertexLayout,
        };
    }

    return candidate;
}
/** @endcond */

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

/**
 * @brief Records staged copies and transitions into one upload command buffer.
 * @param commandBuffer Recording primary buffer owned by ResourceCompiler.
 * @param uploads Valid source, staging, and destination triples.
 */
void recordImageUploads(const vk::raii::CommandBuffer& commandBuffer,
                        std::span<const PendingImageUpload> uploads)
{
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
}
/** @endcond */
} // namespace
} // namespace fire_engine::detail
