#include <fire_engine/render/detail/forward_recorder.hpp>

#include <fire_engine/render/detail/cpu_phase_timer.hpp>
#include <fire_engine/render/detail/draw_binding_state.hpp>
#include <fire_engine/render/detail/draw_constants.hpp>
#include <fire_engine/render/detail/frame_uniforms.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

/**
 * @brief Smallest per-participant draw count at which forward recording is split.
 *
 * Release measurements showed a benefit at 10,000 draws on both
 * decision-bearing implementations in the synthetic benchmark, and a
 * regression on one of them at 1,000. This threshold is that measured boundary
 * rather than an estimate of where the crossover lies, which nothing in those
 * runs locates. It is an empirical policy rather than an interface, so it stays
 * internal and reaches reports only through ForwardRecorder.
 */
constexpr std::size_t kMinimumDrawsPerForwardRecordingParticipant = 5000;

static_assert(std::is_trivially_copyable_v<ForwardPassTarget>);

/* --- File-local function declarations --- */

/**
 * @brief Selects how many participants record one frame when nothing forces a count.
 * @param drawCount Resolved packets in the frozen forward recording input.
 * @return Participant count, at least one and never above the supported maximum.
 */
[[nodiscard]] constexpr std::size_t
automaticForwardParticipantCount(std::size_t drawCount) noexcept;

/**
 * @brief Records fixed state shared by every draw in one command buffer.
 * @param commandBuffer Primary or secondary command buffer receiving the state.
 * @param state Plain handles and dynamic state proved by the input compiler.
 * @return Fresh draw-binding cache scoped to the established descriptor state.
 */
[[nodiscard]] DrawBindingState bindGeometryState(const vk::raii::CommandBuffer& commandBuffer,
                                                 const ForwardRecordingState& state);

/**
 * @brief Records one contiguous ordered range of compiled forward draws.
 * @param commandBuffer Command buffer inside the active forward pass.
 * @param state Compatible pipeline layout shared by every packet.
 * @param draws Contiguous resolved packets recorded in order.
 * @param bindingState Cache created after the complete fixed state was established.
 */
void recordDraws(const vk::raii::CommandBuffer& commandBuffer, const ForwardRecordingState& state,
                 std::span<const ForwardRecordingDraw> draws, DrawBindingState bindingState);

/**
 * @brief Resets one participant's pool and records its chunk into its secondary.
 * @param job Recording context, fixed state, and contiguous packets for this chunk.
 * @param timings Participant-local block receiving timestamps, or null to skip them.
 */
void recordForwardSecondaryChunk(const ForwardSecondaryChunkJob& job,
                                 ChunkRecordingTimings* timings);

/**
 * @brief Merges participant timestamp blocks into the public forward timings.
 * @param chunkTimings Participant-local blocks written during this attempt.
 * @param participants Number of leading blocks that recorded a chunk.
 * @param completionWait Coordinator wait outcome when a helper ran, otherwise null.
 * @param timings Optional public output receiving durations and the critical path.
 */
void mergeChunkTimings(
    const std::array<ChunkRecordingTimings, kMaxForwardRecordingParticipants>& chunkTimings,
    std::size_t participants, const CompletionWait* completionWait, ForwardPassCpuTimings* timings);

/**
 * @brief Transitions the selected color and depth images into attachment layouts.
 * @param commandBuffer Open primary command buffer receiving both barriers.
 * @param target Selected attachment handles and compatibility values.
 */
void transitionToAttachments(const vk::raii::CommandBuffer& commandBuffer,
                             const ForwardPassTarget& target);

/**
 * @brief Begins the forward dynamic-rendering instance.
 * @param commandBuffer Open primary command buffer receiving the rendering boundary.
 * @param target Selected attachment views and render extent.
 * @param flags Rendering flags selecting direct or secondary-command contents.
 */
void beginForwardPass(const vk::raii::CommandBuffer& commandBuffer, const ForwardPassTarget& target,
                      vk::RenderingFlags flags);

/* --- File-local classes --- */

/** @brief Waits for the dispatched forward helper chunk, including while unwinding. */
class ForwardChunkJoin final
{
public:
    /** @brief Adopts a helper with one outstanding chunk. @param helper Dispatched helper. */
    explicit ForwardChunkJoin(ForwardSecondaryRecordingWorker& helper) noexcept
        : helper_{&helper}
    {
    }

    /** @brief Blocks until the helper has stopped reading its job. */
    ~ForwardChunkJoin() noexcept
    {
        helper_->awaitCompletion();
    }

    ForwardChunkJoin(const ForwardChunkJoin&) = delete;
    ForwardChunkJoin& operator=(const ForwardChunkJoin&) = delete;
    ForwardChunkJoin(ForwardChunkJoin&&) = delete;
    ForwardChunkJoin& operator=(ForwardChunkJoin&&) = delete;

private:
    ForwardSecondaryRecordingWorker* helper_; ///< Borrowed for one dispatch.
};

/* --- File-local functions --- */

constexpr std::size_t automaticForwardParticipantCount(std::size_t drawCount) noexcept
{
    const std::size_t supported = drawCount / kMinimumDrawsPerForwardRecordingParticipant;
    if (supported < 2)
    {
        return 1;
    }
    return supported > kMaxForwardRecordingParticipants ? kMaxForwardRecordingParticipants
                                                        : supported;
}

static_assert(automaticForwardParticipantCount(0) == 1);
static_assert(automaticForwardParticipantCount(1) == 1);
static_assert(automaticForwardParticipantCount(1000) == 1);
// One participant below the measured boundary, two at it.
static_assert(automaticForwardParticipantCount(kMinimumDrawsPerForwardRecordingParticipant * 2 -
                                               1) == 1);
static_assert(automaticForwardParticipantCount(kMinimumDrawsPerForwardRecordingParticipant * 2) ==
              2);
// Never above the supported maximum, however large the workload.
static_assert(automaticForwardParticipantCount(kMinimumDrawsPerForwardRecordingParticipant * 100) ==
              kMaxForwardRecordingParticipants);

DrawBindingState bindGeometryState(const vk::raii::CommandBuffer& commandBuffer,
                                   const ForwardRecordingState& state)
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, state.pipeline);
    commandBuffer.setViewport(0, state.viewport);
    commandBuffer.setScissor(0, state.scissor);

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
    return {};
}

void recordDraws(const vk::raii::CommandBuffer& commandBuffer, const ForwardRecordingState& state,
                 std::span<const ForwardRecordingDraw> draws, DrawBindingState bindingState)
{
    constexpr vk::DeviceSize kBufferOffset = 0;
    for (const ForwardRecordingDraw& draw : draws)
    {
        const DrawBindingChanges changes =
            bindingState.update(draw.vertexBuffer, draw.indexBuffer, draw.sampler, draw.imageView);
        if (changes.geometry)
        {
            commandBuffer.bindVertexBuffers(0, draw.vertexBuffer, kBufferOffset);
            commandBuffer.bindIndexBuffer(draw.indexBuffer, 0, vk::IndexType::eUint32);
        }
        if (changes.texture)
        {
            const vk::DescriptorImageInfo textureInfo{
                .sampler = draw.sampler,
                .imageView = draw.imageView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            };
            const vk::WriteDescriptorSet textureWrite{
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &textureInfo,
            };
            commandBuffer.pushDescriptorSet(vk::PipelineBindPoint::eGraphics, state.pipelineLayout,
                                            0, textureWrite);
        }

        commandBuffer.pushConstants<DrawConstants>(
            state.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, draw.constants);
        commandBuffer.drawIndexed(draw.indexCount, 1, 0, 0, 0);
    }
}

void recordForwardSecondaryChunk(const ForwardSecondaryChunkJob& job,
                                 ChunkRecordingTimings* timings)
{
    // Each participant resets its own pool as the first act of its own work.
    // Ordinary frames pass no timing block and take no clock samples.
    if (timings != nullptr)
    {
        timings->resetStart = std::chrono::steady_clock::now();
    }
    job.context->resetCommands();
    if (timings != nullptr)
    {
        timings->resetEnd = std::chrono::steady_clock::now();
    }

    const vk::CommandBufferInheritanceRenderingInfo renderingInheritance{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &job.state.colorAttachmentFormat,
        .depthAttachmentFormat = job.state.depthAttachmentFormat,
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    const vk::CommandBufferInheritanceInfo inheritanceInfo{
        .pNext = &renderingInheritance,
    };
    const vk::CommandBufferBeginInfo secondaryBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
                 vk::CommandBufferUsageFlagBits::eRenderPassContinue,
        .pInheritanceInfo = &inheritanceInfo,
    };
    const vk::raii::CommandBuffer& commandBuffer = job.context->commandBuffer();
    commandBuffer.begin(secondaryBeginInfo);
    // Every chunk establishes its own complete geometry state, so each secondary
    // pays a fixed preamble that does not divide with draw count.
    DrawBindingState bindingState = bindGeometryState(commandBuffer, job.state);
    recordDraws(commandBuffer, job.state, job.draws, std::move(bindingState));
    commandBuffer.end();

    if (timings != nullptr)
    {
        timings->recordEnd = std::chrono::steady_clock::now();
        timings->recorded = true;
    }
}

void mergeChunkTimings(
    const std::array<ChunkRecordingTimings, kMaxForwardRecordingParticipants>& chunkTimings,
    std::size_t participants, const CompletionWait* completionWait, ForwardPassCpuTimings* timings)
{
    if (timings == nullptr)
    {
        return;
    }

    std::chrono::steady_clock::time_point earliestStart{};
    std::chrono::steady_clock::time_point latestResetEnd{};
    std::chrono::steady_clock::time_point latestEnd{};
    for (std::size_t index = 0; index < participants; ++index)
    {
        const ChunkRecordingTimings& chunk = chunkTimings[index];
        assert(chunk.recorded);
        if (index == 0 || chunk.resetStart < earliestStart)
        {
            earliestStart = chunk.resetStart;
        }
        if (index == 0 || chunk.resetEnd > latestResetEnd)
        {
            latestResetEnd = chunk.resetEnd;
        }
        if (index == 0 || chunk.recordEnd > latestEnd)
        {
            latestEnd = chunk.recordEnd;
        }
    }

    for (std::size_t index = 0; index < participants; ++index)
    {
        const ChunkRecordingTimings& chunk = chunkTimings[index];
        const auto poolReset =
            std::chrono::duration_cast<std::chrono::nanoseconds>(chunk.resetEnd - chunk.resetStart);
        const auto recording =
            std::chrono::duration_cast<std::chrono::nanoseconds>(chunk.recordEnd - chunk.resetEnd);
        timings->chunks[index] = ForwardParticipantCpuTimings{
            .poolReset = poolReset,
            .recording = recording,
            .resetStartOffset = std::chrono::duration_cast<std::chrono::nanoseconds>(
                chunk.resetStart - earliestStart),
            .recorded = true,
        };
        timings->workerCommandPoolReset += poolReset;
        timings->secondaryCommandRecording += recording;
    }

    timings->workerResetRegionSpan =
        std::chrono::duration_cast<std::chrono::nanoseconds>(latestResetEnd - earliestStart);
    timings->workerRegionCriticalPath =
        std::chrono::duration_cast<std::chrono::nanoseconds>(latestEnd - earliestStart);

    if (completionWait == nullptr)
    {
        return;
    }
    timings->secondaryJoinWait = std::chrono::duration_cast<std::chrono::nanoseconds>(
        completionWait->end - completionWait->start);
    timings->secondaryCompletionTail =
        std::chrono::duration_cast<std::chrono::nanoseconds>(completionWait->end - latestEnd);
    // Defined independently of finish order: zero when the coordinator was last
    // to finish, so no chunk work remained when it entered the wait.
    timings->secondaryHelperRemainingWork =
        latestEnd > completionWait->start ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                latestEnd - completionWait->start)
                                          : std::chrono::nanoseconds{};
    timings->secondaryCompletionAcquiredBySpin = completionWait->acquiredBySpin;
    timings->secondaryCompletionUsedBlockingWait = completionWait->usedBlockingWait;
}

void transitionToAttachments(const vk::raii::CommandBuffer& commandBuffer,
                             const ForwardPassTarget& target)
{
    // The color image is cleared, so previous presentation contents are discarded.
    const vk::ImageMemoryBarrier2 colorBarrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = target.colorImage,
        .subresourceRange = kColorSubresourceRange,
    };
    commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &colorBarrier,
    });

    // The depth value is cleared before every use. Slot retirement makes it safe
    // to discard that slot's previous writes rather than preserving contents.
    const vk::ImageMemoryBarrier2 depthBarrier{
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
        .pImageMemoryBarriers = &depthBarrier,
    });
}

void beginForwardPass(const vk::raii::CommandBuffer& commandBuffer, const ForwardPassTarget& target,
                      vk::RenderingFlags flags)
{
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = target.colorView,
        .imageLayout = vk::ImageLayout::eAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = {.color = {.float32 = std::array{0.015f, 0.02f, 0.03f, 1.0f}}},
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = target.depthView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = {.depthStencil = {.depth = 1.0f, .stencil = 0}},
    };
    const vk::RenderingInfo renderingInfo{
        .flags = flags,
        .renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    commandBuffer.beginRendering(renderingInfo);
}
/** @endcond */
} // namespace

/* --- Public member functions --- */

ForwardRecorder::ForwardRecorder(ForwardRecordingMode mode,
                                 std::optional<std::size_t> forcedParticipantCount)
    : mode_{mode},
      forcedParticipantCount_{forcedParticipantCount}
{
    if (forcedParticipantCount_.has_value() &&
        (*forcedParticipantCount_ == 0 ||
         *forcedParticipantCount_ > kMaxForwardRecordingParticipants))
    {
        throw std::invalid_argument(
            "Forward recording participant count is outside the supported range");
    }
}

RecordingBufferKind ForwardRecorder::secondaryBufferKind(ForwardRecordingMode mode) noexcept
{
    return mode == ForwardRecordingMode::eSecondaryCommandBuffer ? RecordingBufferKind::eSecondary
                                                                 : RecordingBufferKind::eNone;
}

ForwardRecordingMode ForwardRecorder::mode() const noexcept
{
    return mode_;
}

std::optional<std::size_t> ForwardRecorder::forcedParticipantCount() const noexcept
{
    return forcedParticipantCount_;
}

std::size_t ForwardRecorder::minimumDrawsPerParticipant() const noexcept
{
    return kMinimumDrawsPerForwardRecordingParticipant;
}

void ForwardRecorder::record(const RecordingContext& primary,
                             std::span<const RecordingContext> secondaries,
                             const ForwardRecordingInput& input, const ForwardPassTarget& target,
                             ForwardPassCpuTimings* timings)
{
    assert(primary.hasCommandBuffer());
    assert(!secondaries.empty());
    assert(idle());
    assert(input.state().colorAttachmentFormat == target.colorFormat);
    assert(input.state().depthAttachmentFormat == target.depthFormat);

    const vk::raii::CommandBuffer& primaryCommandBuffer = primary.commandBuffer();
    if (mode_ == ForwardRecordingMode::eDirectPrimary)
    {
        // The direct control retains an empty secondary pool so its fixed reset
        // cost remains measurable against the production path.
        const RecordingContext& emptyContext = secondaries.front();
        assert(!emptyContext.hasCommandBuffer());
        if (timings == nullptr)
        {
            emptyContext.resetCommands();
        }
        else
        {
            const auto resetStart = std::chrono::steady_clock::now();
            emptyContext.resetCommands();
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - resetStart);
            timings->workerCommandPoolReset = elapsed;
            timings->secondaryRecordingRegion = elapsed;
            timings->workerResetRegionSpan = elapsed;
            timings->workerRegionCriticalPath = elapsed;
        }

        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
        transitionToAttachments(primaryCommandBuffer, target);
        beginForwardPass(primaryCommandBuffer, target, {});
        DrawBindingState bindingState = bindGeometryState(primaryCommandBuffer, input.state());
        recordDraws(primaryCommandBuffer, input.state(), input.draws(), std::move(bindingState));
        primaryCommandBuffer.endRendering();
        return;
    }

    assert(secondaries.size() >= kMaxForwardRecordingParticipants);
    const ForwardRecordingState& state = input.state();
    const std::span<const ForwardRecordingDraw> draws = input.draws();
    const std::size_t requested =
        forcedParticipantCount_.value_or(automaticForwardParticipantCount(draws.size()));
    const std::size_t participants = requested > 1 && draws.size() >= requested ? requested : 1;
    const std::size_t firstChunkSize = participants > 1 ? (draws.size() + 1) / 2 : draws.size();

    std::array<ChunkRecordingTimings, kMaxForwardRecordingParticipants> chunkTimings{};
    const auto chunkBlock = [&chunkTimings, timings](std::size_t index)
    { return timings == nullptr ? nullptr : &chunkTimings[index]; };
    const ForwardSecondaryChunkJob coordinatorJob{
        .context = &secondaries.front(),
        .state = state,
        .draws = draws.first(firstChunkSize),
    };
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->secondaryRecordingRegion};
        if (participants > 1)
        {
            const ForwardSecondaryChunkJob helperJob{
                .context = &secondaries[1],
                .state = state,
                .draws = draws.subspan(firstChunkSize),
            };
            helper_.dispatch(&recordForwardSecondaryChunk, helperJob, chunkBlock(1));
            // The guard observes completion while a coordinator failure unwinds.
            // The copied job still borrows the context, draw storage, and timing block.
            const ForwardChunkJoin join{helper_};
            recordForwardSecondaryChunk(coordinatorJob, chunkBlock(0));
        }
        else
        {
            recordForwardSecondaryChunk(coordinatorJob, chunkBlock(0));
        }
    }
    helper_.rethrowIfFailed();
    mergeChunkTimings(chunkTimings, participants,
                      participants > 1 ? &helper_.lastCompletionWait() : nullptr, timings);

    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
        transitionToAttachments(primaryCommandBuffer, target);
        beginForwardPass(primaryCommandBuffer, target,
                         vk::RenderingFlagBits::eContentsSecondaryCommandBuffers);
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->secondaryCommandExecution};
        // Chunk order is preserved so recorded draw order survives the split.
        if (participants > 1)
        {
            const std::array secondaryCommands{*secondaries.front().commandBuffer(),
                                               *secondaries[1].commandBuffer()};
            primaryCommandBuffer.executeCommands(secondaryCommands);
        }
        else
        {
            const std::array secondaryCommands{*secondaries.front().commandBuffer()};
            primaryCommandBuffer.executeCommands(secondaryCommands);
        }
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
        primaryCommandBuffer.endRendering();
    }
}

bool ForwardRecorder::idle() const noexcept
{
    return helper_.idle();
}

} // namespace fire_engine::detail
