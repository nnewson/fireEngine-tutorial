#include <fire_engine/render/renderer.hpp>

#include <fire_engine/core/log.hpp>
#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/graphics/render_preparation.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/allocator.hpp>
#include <fire_engine/render/buffer.hpp>
#include <fire_engine/render/device.hpp>
#include <fire_engine/render/draw_constants.hpp>
#include <fire_engine/render/frame_in_flight.hpp>
#include <fire_engine/render/pipeline.hpp>
#include <fire_engine/render/swapchain.hpp>
#include <fire_engine/scene/scene.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fire_engine
{
namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

/** @brief The sole colour mip and array layer used by every image barrier. */
constexpr vk::ImageSubresourceRange kColorSubresourceRange{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};

/* --- File-local classes --- */

/** @brief GPU buffers compiled from one CPU mesh description. */
class CompiledMesh final
{
public:
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

    [[nodiscard]] const AllocatedBuffer& vertexBuffer() const noexcept
    {
        return vertexBuffer_;
    }

    [[nodiscard]] const AllocatedBuffer& indexBuffer() const noexcept
    {
        return indexBuffer_;
    }

    [[nodiscard]] std::uint32_t indexCount() const noexcept
    {
        return indexCount_;
    }

private:
    AllocatedBuffer vertexBuffer_;
    AllocatedBuffer indexBuffer_;
    std::uint32_t indexCount_;
};

/** @brief Prepared lookup target for one RenderObjectId. */
struct CompiledRenderObject
{
    const CompiledMesh* mesh = nullptr; ///< Shared compiled geometry.
    Color4 baseColour{};                ///< Material factor pushed for each draw.
};
} // namespace

/* --- Renderer implementation --- */

class Renderer::Impl final
{
public:
    Impl(const Glfw& glfw, const Window& window, const std::string& applicationName)
        : device_{glfw, window, applicationName},
          allocator_{device_},
          swapchain_{device_, window},
          pipeline_{device_, swapchain_.imageFormat()},
          frame_{device_, allocator_}
    {
        if (!*device_.graphicsQueue() || !*device_.presentQueue())
        {
            throw std::runtime_error("Vulkan returned a null device queue");
        }
        if (allocator_.handle() == nullptr)
        {
            throw std::runtime_error("VMA returned a null allocator");
        }
        if (swapchain_.imageCount() == 0 ||
            swapchain_.imageViews().size() != swapchain_.images().size() ||
            swapchain_.renderFinished().size() != swapchain_.imageCount())
        {
            throw std::runtime_error("Vulkan returned an incomplete swapchain");
        }
        if (frame_.frameFinished().getStatus() != vk::Result::eSuccess)
        {
            throw std::runtime_error("The frame-finished fence was not initially signaled");
        }
    }

    ~Impl() noexcept
    {
        if (!workMayBePending_)
        {
            return;
        }

        // The explicit waitIdle path reports errors. This raw call is only an
        // exception-unwinding guard before submitted renderer resources die.
        const VkResult result = vkDeviceWaitIdle(static_cast<VkDevice>(*device_.logicalDevice()));
        if (result != VK_SUCCESS)
        {
            fire_engine::log("Vulkan cleanup wait failed with result code {}.",
                             static_cast<std::int32_t>(result));
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void prepare(const RenderAssets& assets, const Scene& scene)
    {
        // Planning validates every CPU relationship before the first Vulkan
        // allocation, keeping malformed input failures deterministic and cheap.
        const SceneDrawList drawList = scene.buildDrawItems();
        const RenderPreparationPlan& plan = renderPreparation_.build(assets, drawList);
        if (compiledGeneration_.has_value() &&
            *compiledGeneration_ == renderPreparation_.generation())
        {
            return;
        }

        // Replacing compiled buffers requires earlier submissions to have
        // finished using them. Identical preparation inputs return above and
        // avoid both this wait and every allocation below.
        if (workMayBePending_)
        {
            waitIdle();
        }

        std::vector<std::unique_ptr<CompiledMesh>> compiledMeshes(assets.meshes().size());
        for (const MeshId meshId : plan.meshes)
        {
            compiledMeshes[meshId.value] =
                std::make_unique<CompiledMesh>(allocator_, assets.meshes()[meshId.value]);
        }

        std::vector<CompiledRenderObject> compiledObjects(assets.renderObjects().size());
        for (const PreparedRenderObject& object : plan.renderObjects)
        {
            compiledObjects[object.id.value] = {
                .mesh = compiledMeshes[object.mesh.value].get(),
                .baseColour = assets.materials()[object.material.value].baseColour,
            };
        }

        compiledMeshes_ = std::move(compiledMeshes);
        compiledObjects_ = std::move(compiledObjects);
        compiledGeneration_ = renderPreparation_.generation();
    }

    [[nodiscard]] RenderResult drawFrame(const Scene& scene)
    {
        if (!compiledGeneration_.has_value())
        {
            throw std::logic_error("Renderer::prepare must be called before drawFrame");
        }
        // Build and validate the transient draw list before acquiring an image.
        // A failure therefore cannot abandon a signaled acquisition semaphore.
        const SceneDrawList drawList = scene.buildDrawItems();
        for (const DrawItem& item : drawList.drawItems)
        {
            if (!item.renderObject.valid() || item.renderObject.value >= compiledObjects_.size() ||
                compiledObjects_[item.renderObject.value].mesh == nullptr)
            {
                throw std::logic_error("Scene refers to an object not compiled by prepare");
            }
        }

        const vk::raii::Device& logicalDevice = device_.logicalDevice();
        const vk::Result fenceResult = logicalDevice.waitForFences(
            *frame_.frameFinished(), vk::True, std::numeric_limits<std::uint64_t>::max());
        if (fenceResult != vk::Result::eSuccess)
        {
            throw vk::SystemError{vk::make_error_code(fenceResult), "Waiting for the frame fence"};
        }

        std::uint32_t imageIndex = 0;
        bool swapchainIsSuboptimal = false;
        try
        {
            const auto [result, acquiredImageIndex] = swapchain_.handle().acquireNextImage(
                std::numeric_limits<std::uint64_t>::max(), *frame_.imageAvailable());
            imageIndex = acquiredImageIndex;
            swapchainIsSuboptimal = result == vk::Result::eSuboptimalKHR;
        }
        catch (const vk::OutOfDateKHRError&)
        {
            // The fence is still signaled because acquisition happens before
            // its reset, so eventual recreation can reuse this frame slot.
            return RenderResult::eNotPresented;
        }

        frame_.resetCommands();
        recordCommands(imageIndex, drawList.drawItems);

        // Nothing intentionally abandons the frame after this reset: a
        // successful submission will signal the fence, while errors unwind.
        logicalDevice.resetFences(*frame_.frameFinished());

        const vk::SemaphoreSubmitInfo waitInfo{
            .semaphore = *frame_.imageAvailable(),
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        };
        const vk::CommandBufferSubmitInfo commandInfo{
            .commandBuffer = *frame_.commandBuffer(),
        };
        const vk::SemaphoreSubmitInfo signalInfo{
            .semaphore = *swapchain_.renderFinished(imageIndex),
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        };
        const vk::SubmitInfo2 submitInfo{
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &waitInfo,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signalInfo,
        };
        device_.graphicsQueue().submit2(submitInfo, *frame_.frameFinished());
        workMayBePending_ = true;

        const vk::Semaphore renderFinished = *swapchain_.renderFinished(imageIndex);
        const vk::SwapchainKHR swapchain = *swapchain_.handle();
        const vk::PresentInfoKHR presentInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinished,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
        };

        try
        {
            if (device_.presentQueue().presentKHR(presentInfo) == vk::Result::eSuboptimalKHR)
            {
                swapchainIsSuboptimal = true;
            }
        }
        catch (const vk::OutOfDateKHRError&)
        {
            return RenderResult::eNotPresented;
        }

        return swapchainIsSuboptimal ? RenderResult::ePresentedSuboptimal
                                     : RenderResult::ePresented;
    }

    void waitIdle()
    {
        device_.logicalDevice().waitIdle();
        workMayBePending_ = false;
    }

    [[nodiscard]] RendererInfo info() const
    {
        return {
            .deviceName = device_.name(),
            .graphicsQueueFamily = device_.graphicsQueueFamily(),
            .presentQueueFamily = device_.presentQueueFamily(),
            .swapchainImageCount = swapchain_.imageCount(),
            .presentationSemaphoreCount = swapchain_.renderFinished().size(),
            .width = swapchain_.extent().width,
            .height = swapchain_.extent().height,
            .imageFormat = vk::to_string(swapchain_.imageFormat()),
            .presentMode = vk::to_string(swapchain_.presentMode()),
        };
    }

private:
    void recordCommands(std::uint32_t imageIndex, const std::vector<DrawItem>& drawItems) const
    {
        const vk::raii::CommandBuffer& commandBuffer = frame_.commandBuffer();
        const vk::CommandBufferBeginInfo beginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
        };
        commandBuffer.begin(beginInfo);

        transitionToAttachment(commandBuffer, imageIndex);
        beginColourPass(commandBuffer, imageIndex);
        recordDraws(commandBuffer, drawItems);
        commandBuffer.endRendering();
        transitionToPresent(commandBuffer, imageIndex);
        commandBuffer.end();
    }

    void transitionToAttachment(const vk::raii::CommandBuffer& commandBuffer,
                                std::uint32_t imageIndex) const
    {
        // The full image is cleared, so previous presentation contents can be
        // discarded instead of tracking a first-use layout for every image.
        const vk::ImageMemoryBarrier2 toAttachment{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain_.image(imageIndex),
            .subresourceRange = kColorSubresourceRange,
        };
        const vk::DependencyInfo beginDependency{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toAttachment,
        };
        commandBuffer.pipelineBarrier2(beginDependency);
    }

    void beginColourPass(const vk::raii::CommandBuffer& commandBuffer,
                         std::uint32_t imageIndex) const
    {
        const vk::ClearValue clearValue{
            .color = {.float32 = std::array{0.015f, 0.02f, 0.03f, 1.0f}},
        };
        const vk::RenderingAttachmentInfo colourAttachment{
            .imageView = *swapchain_.imageView(imageIndex),
            .imageLayout = vk::ImageLayout::eAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearValue,
        };
        const vk::RenderingInfo renderingInfo{
            .renderArea =
                {
                    .offset = {.x = 0, .y = 0},
                    .extent = swapchain_.extent(),
                },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colourAttachment,
        };
        commandBuffer.beginRendering(renderingInfo);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_.pipeline());

        const vk::Viewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(swapchain_.extent().width),
            .height = static_cast<float>(swapchain_.extent().height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const vk::Rect2D scissor{
            .offset = {.x = 0, .y = 0},
            .extent = swapchain_.extent(),
        };
        commandBuffer.setViewport(0, viewport);
        commandBuffer.setScissor(0, scissor);

        const vk::DescriptorBufferInfo uniformInfo{
            .buffer = frame_.uniformBuffer().handle(),
            .offset = 0,
            .range = sizeof(FrameUniforms),
        };
        const vk::WriteDescriptorSet uniformWrite{
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &uniformInfo,
        };
        commandBuffer.pushDescriptorSet(vk::PipelineBindPoint::eGraphics,
                                        *pipeline_.pipelineLayout(), 0, uniformWrite);
    }

    void recordDraws(const vk::raii::CommandBuffer& commandBuffer,
                     const std::vector<DrawItem>& drawItems) const
    {
        constexpr vk::DeviceSize bufferOffset = 0;
        for (const DrawItem& item : drawItems)
        {
            const CompiledRenderObject& object = compiledObjects_[item.renderObject.value];
            const vk::Buffer vertexBuffer = object.mesh->vertexBuffer().handle();
            commandBuffer.bindVertexBuffers(0, vertexBuffer, bufferOffset);
            commandBuffer.bindIndexBuffer(object.mesh->indexBuffer().handle(), 0,
                                          vk::IndexType::eUint32);

            const DrawConstants constants{
                .model = item.world,
                .baseColour = object.baseColour,
            };
            commandBuffer.pushConstants<DrawConstants>(
                *pipeline_.pipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, constants);
            commandBuffer.drawIndexed(object.mesh->indexCount(), 1, 0, 0, 0);
        }
    }

    void transitionToPresent(const vk::raii::CommandBuffer& commandBuffer,
                             std::uint32_t imageIndex) const
    {
        const vk::ImageMemoryBarrier2 toPresent{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eNone,
            .oldLayout = vk::ImageLayout::eAttachmentOptimal,
            .newLayout = vk::ImageLayout::ePresentSrcKHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain_.image(imageIndex),
            .subresourceRange = kColorSubresourceRange,
        };
        const vk::DependencyInfo endDependency{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toPresent,
        };
        commandBuffer.pipelineBarrier2(endDependency);
    }

    // Reverse destruction keeps every allocation ahead of its VMA and Vulkan
    // owners. Presentation lifetime retains the separate Swapchain precondition.

    // Foundational long-lived state.
    Device device_;
    MemoryAllocator allocator_;

    // Presentation-dependent state replaced together when recreation is added.
    Swapchain swapchain_;
    Pipeline pipeline_;

    // Per-frame submission state.
    FrameInFlight frame_;
    bool workMayBePending_ = false;

    // Prepared state compiled from the current scene dependencies.
    RenderPreparation renderPreparation_;
    std::vector<std::unique_ptr<CompiledMesh>> compiledMeshes_;
    std::vector<CompiledRenderObject> compiledObjects_;
    std::optional<std::size_t> compiledGeneration_;
};
/** @endcond */

/* --- Public member functions --- */

Renderer::Renderer(const Glfw& glfw, const Window& window, const std::string& applicationName)
    : implementation_{std::make_unique<Impl>(glfw, window, applicationName)}
{
}

Renderer::~Renderer() noexcept = default;

void Renderer::prepare(const RenderAssets& assets, const Scene& scene)
{
    implementation_->prepare(assets, scene);
}

RenderResult Renderer::drawFrame(const Scene& scene)
{
    return implementation_->drawFrame(scene);
}

void Renderer::waitIdle()
{
    implementation_->waitIdle();
}

RendererInfo Renderer::info() const
{
    return implementation_->info();
}
} // namespace fire_engine
