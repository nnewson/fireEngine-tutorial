#include <fire_engine/render/detail/shadow_recorder.hpp>

#include <fire_engine/render/detail/cpu_phase_timer.hpp>
#include <fire_engine/render/detail/frame_uniforms.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>
#include <fire_engine/render/detail/shadow_draw_constants.hpp>

#include <cassert>
#include <type_traits>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

// Starting raster-bias values. Their visual adequacy depends on the selected
// depth format and must be checked when the forward pass samples the map.
constexpr float kDepthBiasConstantFactor = 1.25f; ///< Constant part of raster depth bias.
constexpr float kDepthBiasClamp = 0.0f;           ///< No clamp on the combined depth bias.
constexpr float kDepthBiasSlopeFactor = 1.75f;    ///< Slope-scaled part of raster depth bias.

static_assert(std::is_trivially_copyable_v<ShadowPassTarget>);

/* --- File-local functions --- */

/**
 * @brief Discards retired map contents and establishes depth-attachment access.
 * @param commandBuffer Open shadow primary receiving the transition.
 * @param target Selected frame-slot map and compatibility values.
 */
void transitionToDepthAttachment(const vk::raii::CommandBuffer& commandBuffer,
                                 const ShadowPassTarget& target)
{
    // Slot-fence retirement completed all prior use, including sampled reads.
    // The source is empty because this transition discards those retired contents.
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
        .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                         vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = target.depthImage,
        .subresourceRange = kDepthSubresourceRange,
    };
    commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}

/**
 * @brief Publishes this pass's depth writes to later fragment sampled reads.
 * @param commandBuffer Shadow primary after its rendering instance has ended.
 * @param target Selected frame-slot map containing the recorded depth writes.
 */
void transitionToSampledDepth(const vk::raii::CommandBuffer& commandBuffer,
                              const ShadowPassTarget& target)
{
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
        .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .newLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = target.depthImage,
        .subresourceRange = kDepthSubresourceRange,
    };
    commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}
} // namespace

/* --- Internal member functions --- */

void ShadowRecorder::record(const RecordingContext& primary, const ShadowRecordingInput& input,
                            const ShadowPassTarget& target, ShadowPassCpuTimings* timings) const
{
    assert(primary.hasCommandBuffer());
    assert(input.state().depthAttachmentFormat == target.depthFormat);

    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->commandPoolReset};
        primary.resetCommands();
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
        const vk::raii::CommandBuffer& commandBuffer = primary.commandBuffer();
        const ShadowRecordingState& state = input.state();
        commandBuffer.begin(vk::CommandBufferBeginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
        });
        transitionToDepthAttachment(commandBuffer, target);

        const vk::RenderingAttachmentInfo depthAttachment{
            .imageView = target.depthView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = {.depthStencil = {.depth = 1.0f, .stencil = 0}},
        };
        commandBuffer.beginRendering(vk::RenderingInfo{
            .renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent},
            .layerCount = 1,
            .colorAttachmentCount = 0,
            .pDepthAttachment = &depthAttachment,
        });
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, state.pipeline);
        const vk::DescriptorBufferInfo uniformInfo{
            .buffer = state.frameUniformBuffer,
            .offset = 0,
            .range = sizeof(FrameUniforms),
        };
        const vk::WriteDescriptorSet uniformWrite{
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &uniformInfo,
        };
        commandBuffer.pushDescriptorSet(vk::PipelineBindPoint::eGraphics, state.pipelineLayout, 0,
                                        uniformWrite);
        // Match forward winding and the registered light-clip-to-texture mapping.
        commandBuffer.setViewport(0, vk::Viewport{
                                         .x = 0.0f,
                                         .y = static_cast<float>(target.extent.height),
                                         .width = static_cast<float>(target.extent.width),
                                         .height = -static_cast<float>(target.extent.height),
                                         .minDepth = 0.0f,
                                         .maxDepth = 1.0f,
                                     });
        commandBuffer.setScissor(0,
                                 vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = target.extent});
        commandBuffer.setDepthBias(kDepthBiasConstantFactor, kDepthBiasClamp,
                                   kDepthBiasSlopeFactor);

        // Geometry reuse is local to this command buffer and requires no material state.
        vk::Buffer previousVertexBuffer;
        vk::Buffer previousIndexBuffer;
        constexpr vk::DeviceSize kBufferOffset = 0;
        for (const ShadowRecordingDraw& draw : input.draws())
        {
            if (draw.vertexBuffer != previousVertexBuffer ||
                draw.indexBuffer != previousIndexBuffer)
            {
                commandBuffer.bindVertexBuffers(0, draw.vertexBuffer, kBufferOffset);
                commandBuffer.bindIndexBuffer(draw.indexBuffer, 0, vk::IndexType::eUint32);
                previousVertexBuffer = draw.vertexBuffer;
                previousIndexBuffer = draw.indexBuffer;
            }
            commandBuffer.pushConstants<ShadowDrawConstants>(
                state.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, draw.constants);
            commandBuffer.drawIndexed(draw.indexCount, 1, 0, 0, 0);
        }
        commandBuffer.endRendering();
        transitionToSampledDepth(commandBuffer, target);
        commandBuffer.end();
    }
    if (timings != nullptr)
    {
        timings->drawCount = input.draws().size();
    }
}
/** @endcond */
} // namespace fire_engine::detail
