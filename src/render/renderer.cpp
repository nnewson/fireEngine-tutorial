#include <fire_engine/render/renderer.hpp>

#include <fire_engine/core/log.hpp>
#include <fire_engine/render/allocator.hpp>
#include <fire_engine/render/device.hpp>
#include <fire_engine/render/pipeline.hpp>
#include <fire_engine/render/swapchain.hpp>
#include <fire_engine/render/vertex.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace fire_engine
{
namespace
{
/* --- File-local constants --- */

/** @brief Counter-clockwise triangle centered in normalized device coordinates. */
constexpr std::array kTriangleVertices = {
    Vertex{.position = {0.0F, -0.6F}, .color = {1.0F, 0.2F, 0.1F}},
    Vertex{.position = {0.6F, 0.6F}, .color = {0.1F, 1.0F, 0.2F}},
    Vertex{.position = {-0.6F, 0.6F}, .color = {0.2F, 0.3F, 1.0F}},
};

/** @brief The sole color mip and array layer used by every image barrier. */
constexpr vk::ImageSubresourceRange kColorSubresourceRange{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};
} // namespace

/* --- Public member functions --- */

Renderer::Renderer(const Device& device, const MemoryAllocator& allocator,
                   const Swapchain& swapchain, const Pipeline& pipeline)
    : device_{device},
      swapchain_{swapchain},
      pipeline_{pipeline},
      vertexBuffer_{allocator, sizeof(kTriangleVertices), vk::BufferUsageFlagBits::eVertexBuffer},
      frame_{device, allocator}
{
    vertexBuffer_.write(std::as_bytes(std::span{kTriangleVertices}));
}

Renderer::~Renderer() noexcept
{
    if (!workMayBePending_)
    {
        return;
    }

    // A destructor cannot report vk::SystemError safely. The explicit waitIdle
    // path handles errors; this raw call is only an exception-unwinding guard.
    const VkResult result = vkDeviceWaitIdle(static_cast<VkDevice>(*device_.logicalDevice()));
    if (result != VK_SUCCESS)
    {
        // Formatting an integer requires no potentially throwing temporary;
        // log itself catches formatting failures to preserve noexcept here.
        fire_engine::log("Vulkan cleanup wait failed with result code {}.",
                         static_cast<std::int32_t>(result));
    }
}

RenderResult Renderer::renderFrame()
{
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
        // The fence is still signaled because acquisition deliberately happens
        // before its reset, so a later recreation can reuse this frame safely.
        return RenderResult::eNotPresented;
    }

    frame_.resetCommands();
    recordCommands(imageIndex);

    // Nothing after this point intentionally abandons the frame: either submit
    // succeeds and signals the fence, or an exception leaves through cleanup.
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
        const vk::Result presentResult = device_.presentQueue().presentKHR(presentInfo);
        if (presentResult == vk::Result::eSuboptimalKHR)
        {
            swapchainIsSuboptimal = true;
        }
    }
    catch (const vk::OutOfDateKHRError&)
    {
        return RenderResult::eNotPresented;
    }

    return swapchainIsSuboptimal ? RenderResult::ePresentedSuboptimal : RenderResult::ePresented;
}

void Renderer::waitIdle()
{
    device_.logicalDevice().waitIdle();
    workMayBePending_ = false;
}

/* --- Private member functions --- */

void Renderer::recordCommands(std::uint32_t imageIndex) const
{
    const vk::raii::CommandBuffer& commandBuffer = frame_.commandBuffer();
    const vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBuffer.begin(beginInfo);

    // Every frame clears the full image, so none of its previous presentation
    // contents need preserving. eUndefined makes that discard explicit and
    // avoids tracking a separate first-use layout for each swapchain image.
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

    const vk::ClearValue clearValue{
        .color = {.float32 = std::array{0.015F, 0.02F, 0.03F, 1.0F}},
    };

    const vk::RenderingAttachmentInfo colorAttachment{
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
        .pColorAttachments = &colorAttachment,
    };
    commandBuffer.beginRendering(renderingInfo);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_.pipeline());

    const vk::Viewport viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(swapchain_.extent().width),
        .height = static_cast<float>(swapchain_.extent().height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    const vk::Rect2D scissor{
        .offset = {.x = 0, .y = 0},
        .extent = swapchain_.extent(),
    };
    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, scissor);

    constexpr vk::DeviceSize vertexOffset = 0;
    const vk::Buffer vertexBuffer = vertexBuffer_.handle();
    commandBuffer.bindVertexBuffers(0, vertexBuffer, vertexOffset);

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
    commandBuffer.pushDescriptorSet(vk::PipelineBindPoint::eGraphics, *pipeline_.pipelineLayout(),
                                    0, uniformWrite);
    commandBuffer.draw(static_cast<std::uint32_t>(kTriangleVertices.size()), 1, 0, 0);

    commandBuffer.endRendering();

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
    commandBuffer.end();
}
} // namespace fire_engine
