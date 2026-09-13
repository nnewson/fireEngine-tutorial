#include <fire_engine/render/renderer.hpp>

#include <fire_engine/core/log.hpp>
#include <fire_engine/graphics/detail/frame_capture.hpp>
#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/graphics/render_preparation.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/capture_format_mapping.hpp>
#include <fire_engine/render/detail/compiled_resource_graph.hpp>
#include <fire_engine/render/detail/compiled_resources.hpp>
#include <fire_engine/render/detail/depth_buffer.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/draw_binding_state.hpp>
#include <fire_engine/render/detail/draw_constants.hpp>
#include <fire_engine/render/detail/forward_recording_input.hpp>
#include <fire_engine/render/detail/forward_secondary_recording_worker.hpp>
#include <fire_engine/render/detail/frame_resources.hpp>
#include <fire_engine/render/detail/frame_slot.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>
#include <fire_engine/render/detail/presentation_state.hpp>
#include <fire_engine/render/detail/readback_buffer.hpp>
#include <fire_engine/render/detail/recording_context.hpp>
#include <fire_engine/render/detail/resource_compiler.hpp>
#include <fire_engine/render/detail/swapchain.hpp>
#include <fire_engine/scene/scene_draw_list.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
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

/**
 * @brief Smallest per-participant draw count at which recording is split.
 *
 * Release measurements showed a benefit at 10,000 draws on both
 * decision-bearing implementations in the synthetic benchmark, and a
 * regression on one of them at 1,000. This threshold is that measured boundary
 * rather than an estimate of where the crossover lies, which nothing in those
 * runs locates. It is an empirical policy rather than an interface, so it stays
 * internal and reaches reports only as a value on RendererInfo.
 */
constexpr std::size_t kMinimumDrawsPerRecordingParticipant = 5000;

/* --- File-local classes --- */

/** @brief Accumulates one optional host phase without adding renderer state. */
class CpuPhaseTimer final
{
public:
    /** @brief Starts timing when output is non-null. @param output Optional phase accumulator. */
    explicit CpuPhaseTimer(std::chrono::nanoseconds* output) noexcept
        : output_{output}
    {
        if (output_ != nullptr)
        {
            start_ = std::chrono::steady_clock::now();
        }
    }

    /** @brief Adds elapsed host time to the supplied accumulator. */
    ~CpuPhaseTimer() noexcept
    {
        if (output_ != nullptr)
        {
            *output_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start_);
        }
    }

    CpuPhaseTimer(const CpuPhaseTimer&) = delete;
    CpuPhaseTimer& operator=(const CpuPhaseTimer&) = delete;
    CpuPhaseTimer(CpuPhaseTimer&&) = delete;
    CpuPhaseTimer& operator=(CpuPhaseTimer&&) = delete;

private:
    std::chrono::nanoseconds* output_ = nullptr;    ///< Optional duration receiving elapsed time.
    std::chrono::steady_clock::time_point start_{}; ///< Start sampled only when output exists.
};

/** @brief Immutable image-copy metadata captured with one selected attempt. */
struct CaptureAttempt final
{
    vk::Extent2D extent;                 ///< Swapchain dimensions recorded by the copy.
    vk::Format imageFormat;              ///< Vulkan format recorded by the copy.
    detail::CaptureFormat captureFormat; ///< Matching byte conversion selected before acquire.
    std::size_t rowPitch = 0;            ///< Tightly packed copy row in bytes.
    std::size_t byteCount = 0;           ///< Complete tightly packed image size.
};

/* --- File-local function declarations --- */

/**
 * @brief Validates and adopts an optional one-shot capture request.
 * @param request Application-owned request moved into renderer lifetime.
 * @return Valid request, or no value when capture is disabled.
 * @throws std::invalid_argument if an enabled request is incomplete.
 */
[[nodiscard]] std::optional<FrameCaptureRequest>
validatedCaptureRequest(std::optional<FrameCaptureRequest> request);

[[nodiscard]] constexpr vk::Viewport sceneViewport(vk::Extent2D extent) noexcept;

/**
 * @brief Selects how many participants record one frame when nothing forces a count.
 * @param drawCount Resolved packets in the frozen forward recording input.
 * @return Participant count, at least one and never above the supported maximum.
 */
[[nodiscard]] constexpr std::size_t automaticParticipantCount(std::size_t drawCount) noexcept;

/**
 * @brief Records fixed state shared by every draw in one command buffer.
 * @param commandBuffer Primary or secondary command buffer receiving the state.
 * @param state Plain handles and dynamic state proved by the input compiler.
 * @return Fresh draw-binding cache scoped to the established descriptor state.
 */
[[nodiscard]] detail::DrawBindingState
bindGeometryState(const vk::raii::CommandBuffer& commandBuffer,
                  const detail::ForwardRecordingState& state);

/**
 * @brief Records the bindings, constants, and indexed draw for each packet.
 * @param commandBuffer Command buffer inside the active color pass.
 * @param state Compatible pipeline layout shared by every packet.
 * @param draws Contiguous resolved packets recorded in order.
 * @param bindingState Cache created when the complete geometry state was established.
 */
void recordDraws(const vk::raii::CommandBuffer& commandBuffer,
                 const detail::ForwardRecordingState& state,
                 std::span<const detail::ForwardRecordingDraw> draws,
                 detail::DrawBindingState bindingState);

/**
 * @brief Resets one participant's pool and records its chunk into its secondary.
 * @param job Recording context, fixed state, and contiguous packets for this chunk.
 * @param timings Participant-local block receiving this chunk's timestamps.
 */
void recordForwardSecondaryChunk(const detail::ForwardSecondaryChunkJob& job,
                                 detail::ChunkRecordingTimings* timings);

/**
 * @brief Merges participant timestamp blocks into the public per-frame timings.
 * @param chunkTimings Participant-local blocks written during this attempt.
 * @param participants Number of leading blocks that recorded a chunk.
 * @param completionWait Coordinator wait outcome when a helper ran, otherwise null.
 * @param timings Optional public output receiving durations and the critical path.
 */
void mergeChunkTimings(
    const std::array<detail::ChunkRecordingTimings, kMaxSecondaryRecordingThreads>& chunkTimings,
    std::size_t participants, const detail::CompletionWait* completionWait,
    RendererCpuTimings* timings);

/* --- File-local classes --- */

/** @brief Waits for the dispatched forward helper chunk, including while unwinding. */
class ForwardChunkJoin final
{
public:
    /** @brief Adopts a helper with one outstanding chunk. @param helper Dispatched helper. */
    explicit ForwardChunkJoin(detail::ForwardSecondaryRecordingWorker& helper) noexcept
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
    detail::ForwardSecondaryRecordingWorker* helper_; ///< Borrowed for one dispatch.
};
} // namespace

/* --- Private implementation class declaration --- */

/** @brief Vulkan-owning implementation hidden behind the public Renderer facade. */
class Renderer::Impl final
{
public:
    /**
     * @brief Creates the complete Vulkan ownership tree for one window.
     * @param glfw Initialized platform lifetime owner.
     * @param window Window used for surface and swapchain creation.
     * @param applicationName Name reported to Vulkan.
     * @param configuration Fixed command-recording choices for this renderer.
     */
    Impl(const Glfw& glfw, const Window& window, const std::string& applicationName,
         RendererConfiguration configuration);

    /** @brief Waits defensively for pending work during exceptional unwinding. */
    ~Impl() noexcept;

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    /**
     * @brief Compiles the asset subset referenced by the current scene.
     * @param assets Complete Vulkan-free asset catalog.
     * @param drawList Frozen draw dependencies selecting the compiled subset.
     */
    void prepare(const RenderAssets& assets, const SceneDrawList& drawList);

    /**
     * @brief Records, submits, and presents the current scene once.
     * @param drawList Frozen draw items and transforms valid until this call returns.
     * @param camera Application-owned perspective values sampled for this frame.
     * @param timings Optional output populated with host timings for this attempt.
     * @return Presentation outcome for the acquired swapchain image.
     */
    [[nodiscard]] RenderResult drawFrame(const SceneDrawList& drawList, const Camera& camera,
                                         RendererCpuTimings* timings);

    /**
     * @brief Replaces the complete presentation-dependent ownership group.
     * @param framebufferExtent Drawable size sampled by the application event loop.
     * @return true after replacement, or false for a transient zero-sized framebuffer.
     */
    [[nodiscard]] bool recreatePresentation(FramebufferExtent framebufferExtent);

    /** @brief Waits for device work and clears pending-submission bookkeeping. */
    void waitIdle();

    /** @brief Describes the selected Vulkan and presentation state. @return Public summary. */
    [[nodiscard]] RendererInfo info() const;

    /** @brief Reports whether a requested one-shot capture committed. @return Completion state. */
    [[nodiscard]] bool captureComplete() const noexcept;

private:
    /**
     * @brief Records the complete command-buffer sequence for one acquired image.
     * @param frameSlotIndex Cycled submission slot whose command buffers are reusable.
     * @param imageIndex Acquired swapchain-image index.
     * @param input Compiler-produced immutable forward recording input.
     * @param captureAttempt Selected readback copy, or null for ordinary rendering.
     * @param timings Optional output receiving the serial and secondary recording phases.
     */
    void recordCommands(std::size_t frameSlotIndex, std::uint32_t imageIndex,
                        const detail::ForwardRecordingInput& input,
                        const CaptureAttempt* captureAttempt, RendererCpuTimings* timings);

    /**
     * @brief Records inherited draws and executes them from one primary geometry pass.
     * @param frameSlotIndex Cycled submission slot owning this frame's recording contexts.
     * @param imageIndex Acquired swapchain-image index.
     * @param input Compiler-produced immutable forward recording input.
     * @param captureAttempt Selected readback copy, or null for ordinary rendering.
     * @param timings Optional output receiving both command-buffer recording phases.
     */
    void recordSecondaryCommands(std::size_t frameSlotIndex, std::uint32_t imageIndex,
                                 const detail::ForwardRecordingInput& input,
                                 const CaptureAttempt* captureAttempt, RendererCpuTimings* timings);

    /**
     * @brief Records the complete geometry pass directly into one primary command buffer.
     * @param frameSlotIndex Cycled submission slot owning this frame's recording context.
     * @param imageIndex Acquired swapchain-image index.
     * @param input Compiler-produced immutable forward recording input.
     * @param captureAttempt Selected readback copy, or null for ordinary rendering.
     * @param timings Optional output receiving the direct primary recording phase.
     */
    void recordDirectCommands(std::size_t frameSlotIndex, std::uint32_t imageIndex,
                              const detail::ForwardRecordingInput& input,
                              const CaptureAttempt* captureAttempt, RendererCpuTimings* timings);

    /**
     * @brief Begins a primary command buffer and its geometry rendering instance.
     * @param commandBuffer Primary command buffer receiving the frame prefix.
     * @param frameSlotIndex Cycled submission slot selecting its depth attachment.
     * @param imageIndex Acquired swapchain-image index used as the color attachment.
     * @param flags Rendering flags selecting direct or secondary-command contents.
     */
    void beginPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer,
                               std::size_t frameSlotIndex, std::uint32_t imageIndex,
                               vk::RenderingFlags flags) const;

    /**
     * @brief Ends the geometry instance and records the primary command-buffer suffix.
     * @param commandBuffer Primary command buffer receiving the frame suffix.
     * @param imageIndex Acquired swapchain-image index transitioned for presentation.
     * @param captureAttempt Selected readback copy, or null for an attachment-to-present suffix.
     */
    void endPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer, std::uint32_t imageIndex,
                             const CaptureAttempt* captureAttempt) const;

    /**
     * @brief Orders acquisition before the transition to color-attachment use.
     * @param commandBuffer Command buffer receiving the image barrier.
     * @param imageIndex Acquired swapchain-image index.
     */
    void transitionToAttachment(const vk::raii::CommandBuffer& commandBuffer,
                                std::uint32_t imageIndex) const;

    /**
     * @brief Discards earlier depth and transitions it for this frame's writes.
     * @param commandBuffer Command buffer receiving the image barrier.
     * @param frameSlotIndex Cycled submission slot selecting its depth attachment.
     */
    void transitionDepthToAttachment(const vk::raii::CommandBuffer& commandBuffer,
                                     std::size_t frameSlotIndex) const;

    /**
     * @brief Begins dynamic rendering for direct or secondary-command contents.
     * @param commandBuffer Primary command buffer receiving the rendering boundary.
     * @param frameSlotIndex Cycled submission slot selecting its depth attachment.
     * @param imageIndex Acquired swapchain-image index used as the color attachment.
     * @param flags Rendering flags selecting direct or secondary-command contents.
     */
    void beginGeometryPass(const vk::raii::CommandBuffer& commandBuffer, std::size_t frameSlotIndex,
                           std::uint32_t imageIndex, vk::RenderingFlags flags) const;

    /** @brief Reports whether any submission slot may still be in use. @return Pending state. */
    [[nodiscard]] bool workMayBePending() const noexcept;

    /**
     * @brief Selects the buffer a worker context allocates for the active recording path.
     * @return Secondary for the production path, or none for the direct-primary control.
     */
    [[nodiscard]] detail::RecordingBufferKind workerBufferKind() const noexcept;

    /**
     * @brief Orders color writes before the transition to presentation.
     * @param commandBuffer Command buffer receiving the image barrier.
     * @param imageIndex Acquired swapchain-image index.
     */
    void transitionToPresent(const vk::raii::CommandBuffer& commandBuffer,
                             std::uint32_t imageIndex) const;

    /**
     * @brief Transitions and copies one rendered image into the readback allocation.
     * @param commandBuffer Primary command buffer receiving the capture suffix.
     * @param imageIndex Acquired swapchain-image index copied before presentation.
     * @param attempt Extent and tightly packed readback layout recorded for this attempt.
     */
    void recordCapture(const vk::raii::CommandBuffer& commandBuffer, std::uint32_t imageIndex,
                       const CaptureAttempt& attempt) const;

    /**
     * @brief Allocates readback storage and snapshots the current presentation description.
     * @return Metadata for recording and decoding the selected attempt.
     */
    [[nodiscard]] CaptureAttempt prepareCaptureAttempt();

    /**
     * @brief Waits for one submitted slot before host access to its readback bytes.
     * @param frameSlot Slot whose submission fence was signaled by the capture copy.
     */
    void waitForCaptureSubmission(const detail::FrameSlot& frameSlot) const;

    /**
     * @brief Invalidates, converts, and writes one successfully presented capture.
     * @param attempt Extent and format snapshot used when the copy was recorded.
     */
    void commitCapture(const CaptureAttempt& attempt);

    // Reverse destruction keeps every allocation ahead of its VMA and Vulkan
    // owners. Presentation lifetime retains the separate Swapchain precondition.

    // Foundational long-lived state.
    CommandRecordingMode commandRecordingMode_; ///< Fixed production or attribution path.
    /// Diagnostic override, or unset when the workload selects the participant count.
    std::optional<std::size_t> forcedSecondaryRecordingThreadCount_;
    std::optional<FrameCaptureRequest> captureRequest_; ///< Owned one-shot diagnostic request.
    detail::Device device_;                     ///< Vulkan instance, surface, device, and queues.
    detail::MemoryAllocator allocator_;         ///< VMA owner created from the logical device.
    detail::ResourceCompiler resourceCompiler_; ///< Dedicated setup-time upload context.
    std::unique_ptr<detail::ReadbackBuffer> readbackBuffer_; ///< Lazy one-shot capture storage.

    // Presentation-dependent state replaced as one ownership group.
    std::unique_ptr<detail::PresentationState> presentation_; ///< Swapchain-compatible resources.

    std::array<detail::FrameResources, detail::kFrameSlotCount>
        frames_;                            ///< Presentation-independent slot state.
    std::size_t nextFrameSlotIndex_ = 0;    ///< Slot selected independently of acquired images.
    std::uint64_t presentedFrameCount_ = 0; ///< Successful presentations seen by capture logic.
    std::uint64_t presentationRecreationCount_ = 0; ///< Completed presentation replacements.
    bool captureComplete_ = false; ///< Whether the configured PNG committed successfully.

    // Prepared state compiled from the current scene dependencies.
    RenderPreparation renderPreparation_;           ///< Vulkan-free validation and plan cache.
    detail::CompiledResources compiledResources_;   ///< GPU state selected by the current plan.
    std::optional<std::size_t> compiledGeneration_; ///< Plan generation uploaded to the GPU.
    detail::ForwardRecordingInputCompiler
        forwardRecordingInputCompiler_; ///< Reusable forward packet-freeze arena.

    // Declared last so reverse member destruction stops the helper before the
    // recording contexts whose pools it writes into.
    detail::ForwardSecondaryRecordingWorker
        forwardSecondaryHelper_; ///< Records the second forward chunk on request.
};
/** @endcond */

/* --- Public member functions --- */

Renderer::Renderer(const Glfw& glfw, const Window& window, const std::string& applicationName,
                   RendererConfiguration configuration)
    : implementation_{
          std::make_unique<Impl>(glfw, window, applicationName, std::move(configuration))}
{
}

Renderer::~Renderer() noexcept = default;

void Renderer::prepare(const RenderAssets& assets, const SceneDrawList& drawList)
{
    implementation_->prepare(assets, drawList);
}

RenderResult Renderer::drawFrame(const SceneDrawList& drawList, const Camera& camera,
                                 RendererCpuTimings* timings)
{
    return implementation_->drawFrame(drawList, camera, timings);
}

bool Renderer::recreatePresentation(FramebufferExtent framebufferExtent)
{
    return implementation_->recreatePresentation(framebufferExtent);
}

bool Renderer::captureComplete() const noexcept
{
    return implementation_->captureComplete();
}

void Renderer::waitIdle()
{
    implementation_->waitIdle();
}

RendererInfo Renderer::info() const
{
    return implementation_->info();
}

/** @cond INTERNAL */
/* --- Private implementation member functions --- */

Renderer::Impl::Impl(const Glfw& glfw, const Window& window, const std::string& applicationName,
                     RendererConfiguration configuration)
    : commandRecordingMode_{configuration.commandRecordingMode},
      forcedSecondaryRecordingThreadCount_{configuration.forcedSecondaryRecordingThreadCount},
      // Validate the Vulkan-free request before constructing the device and
      // capture-enabled swapchain.
      captureRequest_{validatedCaptureRequest(std::move(configuration.captureRequest))},
      device_{glfw, window, applicationName},
      allocator_{device_},
      resourceCompiler_{device_, allocator_},
      presentation_{std::make_unique<detail::PresentationState>(
          device_, allocator_, window.framebufferExtent(), captureRequest_.has_value())},
      // Frame resources are presentation-independent. Their synchronization
      // objects and recording contexts borrow the device, while their uniform
      // storage borrows the allocator; both owners are declared earlier.
      frames_{detail::FrameResources{
                  .slot = detail::FrameSlot{device_},
                  .forwardUniforms = detail::ForwardFrameUniformBuffer{allocator_},
                  .coordinator =
                      detail::RecordingContext{device_, detail::RecordingBufferKind::ePrimary},
                  .secondaries = {detail::RecordingContext{device_, workerBufferKind()},
                                  detail::RecordingContext{device_, workerBufferKind()}}},
              detail::FrameResources{
                  .slot = detail::FrameSlot{device_},
                  .forwardUniforms = detail::ForwardFrameUniformBuffer{allocator_},
                  .coordinator =
                      detail::RecordingContext{device_, detail::RecordingBufferKind::ePrimary},
                  .secondaries = {detail::RecordingContext{device_, workerBufferKind()},
                                  detail::RecordingContext{device_, workerBufferKind()}}}}
{
    if (forcedSecondaryRecordingThreadCount_.has_value() &&
        (*forcedSecondaryRecordingThreadCount_ == 0 ||
         *forcedSecondaryRecordingThreadCount_ > kMaxSecondaryRecordingThreads))
    {
        throw std::invalid_argument("Secondary recording thread count is outside the supported "
                                    "range");
    }
    if (!*device_.graphicsQueue() || !*device_.presentQueue())
    {
        throw std::runtime_error("Vulkan returned a null device queue");
    }
    if (allocator_.handle() == nullptr)
    {
        throw std::runtime_error("VMA returned a null allocator");
    }
    for (const detail::FrameResources& frame : frames_)
    {
        if (frame.slot.frameFinished().getStatus() != vk::Result::eSuccess)
        {
            throw std::runtime_error("A frame-finished fence was not initially signaled");
        }
    }
}

Renderer::Impl::~Impl() noexcept
{
    // drawFrame() waits for the helper synchronously, so every entry point
    // below begins with it idle. Assert the invariant rather than joining,
    // which would defeat the helper's persistence.
    assert(forwardSecondaryHelper_.idle());
    if (!workMayBePending())
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
        return;
    }

    try
    {
        presentation_->waitForPresentations();
    }
    catch (const std::exception& error)
    {
        fire_engine::log("Presentation cleanup wait failed: {}.", error.what());
    }
}

void Renderer::Impl::prepare(const RenderAssets& assets, const SceneDrawList& drawList)
{
    assert(forwardSecondaryHelper_.idle());
    // Planning validates every CPU relationship before the first Vulkan
    // allocation, keeping malformed input failures deterministic and cheap.
    const RenderPreparationPlan& plan =
        renderPreparation_.build(assets, drawList, presentation_->forwardPipeline().description());
    if (compiledGeneration_.has_value() && *compiledGeneration_ == renderPreparation_.generation())
    {
        return;
    }

    // Replacing compiled buffers requires earlier submissions to have
    // finished using them. Identical preparation inputs return above and
    // avoid both this wait and every allocation below.
    if (workMayBePending())
    {
        waitIdle();
    }

    std::unique_ptr<detail::CompiledResourceGraph> candidate =
        resourceCompiler_.compile(assets, plan, compiledResources_.graph());
    compiledResources_.replace(std::move(candidate));
    compiledGeneration_ = renderPreparation_.generation();
}

RenderResult Renderer::Impl::drawFrame(const SceneDrawList& drawList, const Camera& camera,
                                       RendererCpuTimings* timings)
{
    if (timings != nullptr)
    {
        *timings = {};
    }
    if (!compiledGeneration_.has_value())
    {
        throw std::logic_error("Renderer::prepare must be called before drawFrame");
    }
    std::optional<CaptureAttempt> captureAttempt;
    if (captureRequest_.has_value() && !captureComplete_ &&
        presentedFrameCount_ + 1 == captureRequest_->frameOrdinal)
    {
        // Allocation and format validation precede acquisition. A failure
        // therefore cannot abandon a signaled image-available semaphore.
        captureAttempt.emplace(prepareCaptureAttempt());
    }
    const std::size_t frameSlotIndex = nextFrameSlotIndex_;
    detail::FrameResources& frame = frames_[frameSlotIndex];
    detail::FrameSlot& frameSlot = frame.slot;
    // Freeze external IDs and transforms before acquisition. A compiler
    // failure therefore cannot abandon a signaled acquisition semaphore.
    const detail::ForwardRecordingInput forwardRecordingInput =
        [&]() -> detail::ForwardRecordingInput
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->recordingInputBuild};
        const vk::Extent2D extent = presentation_->swapchain().extent();
        const detail::ForwardRecordingState forwardRecordingState{
            .pipeline = *presentation_->forwardPipeline().pipeline(),
            .pipelineLayout = *presentation_->forwardPipeline().pipelineLayout(),
            .frameUniformBuffer = frame.forwardUniforms.handle(),
            .frameUniforms = {.viewProjection = cameraViewProjection(
                                  camera, static_cast<float>(extent.width) /
                                              static_cast<float>(extent.height))},
            .viewport = sceneViewport(extent),
            .scissor = {.offset = {.x = 0, .y = 0}, .extent = extent},
            .colorAttachmentFormat = presentation_->swapchain().imageFormat(),
            .depthAttachmentFormat = presentation_->depthBuffer(frameSlotIndex).format(),
            .vertexLayout = presentation_->forwardPipeline().description().vertexLayout,
        };
        return forwardRecordingInputCompiler_.compile(drawList, compiledResources_.view(),
                                                      forwardRecordingState);
    }();

    const vk::raii::Device& logicalDevice = device_.logicalDevice();
    vk::Result fenceResult = vk::Result::eSuccess;
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->frameFenceWait};
        fenceResult = logicalDevice.waitForFences(*frameSlot.frameFinished(), vk::True,
                                                  std::numeric_limits<std::uint64_t>::max());
    }
    if (fenceResult != vk::Result::eSuccess)
    {
        throw vk::SystemError{vk::make_error_code(fenceResult), "Waiting for the frame fence"};
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->frameUniformUpdate};
        frame.forwardUniforms.update(forwardRecordingInput.state().frameUniforms);
    }

    std::uint32_t imageIndex = 0;
    bool swapchainIsSuboptimal = false;
    try
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->imageAcquisitionWait};
        const auto [result, acquiredImageIndex] =
            presentation_->swapchain().handle().acquireNextImage(
                std::numeric_limits<std::uint64_t>::max(), *frameSlot.imageAvailable());
        imageIndex = acquiredImageIndex;
        swapchainIsSuboptimal = result == vk::Result::eSuboptimalKHR;
    }
    catch (const vk::OutOfDateKHRError&)
    {
        // The fence is still signaled because acquisition happens before
        // its reset. Do not advance the cycle when this slot submitted nothing.
        return RenderResult::eNotPresented;
    }

    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->presentationFenceWait};
        presentation_->preparePresentFence(imageIndex);
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->coordinatorCommandPoolReset};
        frame.coordinator.resetCommands();
    }
    recordCommands(frameSlotIndex, imageIndex, forwardRecordingInput,
                   captureAttempt.has_value() ? &*captureAttempt : nullptr, timings);
    if (timings != nullptr)
    {
        timings->commandPoolReset =
            timings->coordinatorCommandPoolReset + timings->workerCommandPoolReset;
    }

    // Nothing intentionally abandons the frame after this reset: a
    // successful submission will signal the fence, while errors unwind.
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->queueSubmission};
        logicalDevice.resetFences(*frameSlot.frameFinished());

        const vk::SemaphoreSubmitInfo waitInfo{
            .semaphore = *frameSlot.imageAvailable(),
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        };
        const vk::CommandBufferSubmitInfo commandInfo{
            .commandBuffer = *frame.coordinator.commandBuffer(),
        };
        const vk::SemaphoreSubmitInfo signalInfo{
            .semaphore = *presentation_->swapchain().renderFinished(imageIndex),
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        };
        const vk::SubmitInfo2 submitInfo{
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &waitInfo,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
            // Capture retires this complete submission on the host before
            // presentation. Do not signal a binary semaphore that the capture
            // presentation will not consume; it would remain signaled when
            // this swapchain image is reused.
            .signalSemaphoreInfoCount = captureAttempt.has_value() ? 0U : 1U,
            .pSignalSemaphoreInfos = captureAttempt.has_value() ? nullptr : &signalInfo,
        };
        device_.graphicsQueue().submit2(submitInfo, *frameSlot.frameFinished());
    }
    frameSlot.markWorkPending();
    nextFrameSlotIndex_ = (frameSlotIndex + 1) % detail::kFrameSlotCount;
    if (captureAttempt.has_value())
    {
        // Capture is a one-shot diagnostic, so retire the complete submission
        // before enqueueing presentation. This host wait guarantees that the
        // copy and final present transition have completed. Ordinary frames
        // retain their presentation-semaphore dependency.
        //
        // KosmicKrisp's technical-preview driver stalled when presentation
        // waited on a semaphore whose signal covered the capture transfer.
        // Re-test that GPU-side path when a release driver is available; this
        // bounded diagnostic path deliberately favours portability over overlap.
        waitForCaptureSubmission(frameSlot);
    }

    const vk::Semaphore renderFinished = *presentation_->swapchain().renderFinished(imageIndex);
    const vk::SwapchainKHR swapchain = *presentation_->swapchain().handle();
    const vk::Fence presentFence = *presentation_->presentFence(imageIndex);
    const vk::SwapchainPresentFenceInfoKHR presentFenceInfo{
        .swapchainCount = 1,
        .pFences = &presentFence,
    };
    const vk::PresentInfoKHR presentInfo{
        .pNext = &presentFenceInfo,
        // The capture submission was retired on the host before this call.
        // Ordinary frames retain the GPU-side semaphore dependency.
        .waitSemaphoreCount = captureAttempt.has_value() ? 0U : 1U,
        .pWaitSemaphores = captureAttempt.has_value() ? nullptr : &renderFinished,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
    };

    try
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->presentation};
        if (device_.presentQueue().presentKHR(presentInfo) == vk::Result::eSuboptimalKHR)
        {
            swapchainIsSuboptimal = true;
        }
        presentation_->markPresentSubmitted(imageIndex);
    }
    catch (const vk::OutOfDateKHRError&)
    {
        // Out-of-date still enqueues the presentation operation and its fence.
        presentation_->markPresentSubmitted(imageIndex);
        // A selected capture was already retired before presentation, but this
        // ordinal did not present and is therefore discarded and retried.
        return RenderResult::eNotPresented;
    }

    ++presentedFrameCount_;
    if (captureAttempt.has_value())
    {
        commitCapture(*captureAttempt);
    }

    return swapchainIsSuboptimal ? RenderResult::ePresentedSuboptimal : RenderResult::ePresented;
}

void Renderer::Impl::waitIdle()
{
    assert(forwardSecondaryHelper_.idle());
    device_.logicalDevice().waitIdle();
    presentation_->waitForPresentations();
    for (detail::FrameResources& frame : frames_)
    {
        frame.slot.clearPendingWork();
    }
}

bool Renderer::Impl::workMayBePending() const noexcept
{
    for (const detail::FrameResources& frame : frames_)
    {
        if (frame.slot.workMayBePending())
        {
            return true;
        }
    }
    return false;
}

detail::RecordingBufferKind Renderer::Impl::workerBufferKind() const noexcept
{
    return commandRecordingMode_ == CommandRecordingMode::eSecondaryCommandBuffer
               ? detail::RecordingBufferKind::eSecondary
               : detail::RecordingBufferKind::eNone;
}

bool Renderer::Impl::recreatePresentation(FramebufferExtent framebufferExtent)
{
    assert(forwardSecondaryHelper_.idle());
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        return false;
    }

    // Device idle covers command-buffer use of the old color and depth images.
    // Presentation fences separately prove that the presentation engine has
    // released the old swapchain and its binary wait semaphores.
    waitIdle();
    const vk::SwapchainKHR oldSwapchain = *presentation_->swapchain().handle();
    auto replacement = std::make_unique<detail::PresentationState>(
        device_, allocator_, framebufferExtent, captureRequest_.has_value(), oldSwapchain);
    presentation_ = std::move(replacement);
    ++presentationRecreationCount_;
    return true;
}

bool Renderer::Impl::captureComplete() const noexcept
{
    return captureComplete_;
}

RendererInfo Renderer::Impl::info() const
{
    return {
        .deviceName = device_.name(),
        .driverName = device_.driverName(),
        .driverInfo = device_.driverInfo(),
        .graphicsQueueFamily = device_.graphicsQueueFamily(),
        .presentQueueFamily = device_.presentQueueFamily(),
        .frameSlotCount = frames_.size(),
        .swapchainImageCount = presentation_->swapchain().imageCount(),
        .presentationSemaphoreCount = presentation_->swapchain().renderFinished().size(),
        .width = presentation_->swapchain().extent().width,
        .height = presentation_->swapchain().extent().height,
        .imageFormat = vk::to_string(presentation_->swapchain().imageFormat()),
        .depthFormat = vk::to_string(presentation_->depthBuffer(0).format()),
        .presentMode = vk::to_string(presentation_->swapchain().presentMode()),
        .commandRecordingMode = commandRecordingMode_,
        .forcedSecondaryRecordingThreadCount = forcedSecondaryRecordingThreadCount_,
        .minimumDrawsPerRecordingParticipant = kMinimumDrawsPerRecordingParticipant,
    };
}

void Renderer::Impl::recordCommands(std::size_t frameSlotIndex, std::uint32_t imageIndex,
                                    const detail::ForwardRecordingInput& input,
                                    const CaptureAttempt* captureAttempt,
                                    RendererCpuTimings* timings)
{
    if (commandRecordingMode_ == CommandRecordingMode::eDirectPrimary)
    {
        // The direct control still owns a secondary pool and still resets it, so
        // its empty-pool cost stays measurable and comparable. That reset is the
        // whole of its secondary-recording region.
        const detail::RecordingContext& emptyContext = frames_[frameSlotIndex].secondaries.front();
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
            // No chunk is marked recorded: this path records no secondary at
            // all, so it has zero recording participants even though its empty
            // pool is still reset and timed as the fixed-cost floor.
            timings->workerCommandPoolReset = elapsed;
            timings->secondaryRecordingRegion = elapsed;
            timings->workerResetRegionSpan = elapsed;
            timings->workerRegionCriticalPath = elapsed;
        }
        recordDirectCommands(frameSlotIndex, imageIndex, input, captureAttempt, timings);
        return;
    }
    recordSecondaryCommands(frameSlotIndex, imageIndex, input, captureAttempt, timings);
}

void Renderer::Impl::recordSecondaryCommands(std::size_t frameSlotIndex, std::uint32_t imageIndex,
                                             const detail::ForwardRecordingInput& input,
                                             const CaptureAttempt* captureAttempt,
                                             RendererCpuTimings* timings)
{
    const detail::FrameResources& frame = frames_[frameSlotIndex];
    const detail::ForwardRecordingState& state = input.state();
    const std::span<const detail::ForwardRecordingDraw> draws = input.draws();

    // The production policy selects the count from the workload. A diagnostic
    // override replaces that choice so a forced split can be measured below the
    // threshold and compared with an explicitly single-participant control.
    // Either way the ranges must stay non-empty.
    const std::size_t requested =
        forcedSecondaryRecordingThreadCount_.value_or(automaticParticipantCount(draws.size()));
    const std::size_t participants = requested > 1 && draws.size() >= requested ? requested : 1;
    const std::size_t firstChunkSize = participants > 1 ? (draws.size() + 1) / 2 : draws.size();

    std::array<detail::ChunkRecordingTimings, kMaxSecondaryRecordingThreads> chunkTimings{};
    const auto chunkBlock = [&chunkTimings, timings](std::size_t index)
    { return timings == nullptr ? nullptr : &chunkTimings[index]; };
    const detail::ForwardSecondaryChunkJob coordinatorJob{
        .context = &frame.secondaries.front(),
        .state = state,
        .draws = draws.first(firstChunkSize),
    };
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->secondaryRecordingRegion};
        if (participants > 1)
        {
            const detail::ForwardSecondaryChunkJob helperJob{
                .context = &frame.secondaries[1],
                .state = state,
                .draws = draws.subspan(firstChunkSize),
            };
            forwardSecondaryHelper_.dispatch(&recordForwardSecondaryChunk, helperJob,
                                             chunkBlock(1));
            // The guard's destructor waits for the helper, so completion is
            // observed even while an exception from the coordinator's own chunk
            // unwinds this scope. The job itself was copied by dispatch; what
            // must stay alive is the context, draw storage, and timing block.
            const ForwardChunkJoin join{forwardSecondaryHelper_};
            recordForwardSecondaryChunk(coordinatorJob, chunkBlock(0));
        }
        else
        {
            recordForwardSecondaryChunk(coordinatorJob, chunkBlock(0));
        }
    }
    forwardSecondaryHelper_.rethrowIfFailed();
    mergeChunkTimings(chunkTimings, participants,
                      participants > 1 ? &forwardSecondaryHelper_.lastCompletionWait() : nullptr,
                      timings);

    const vk::raii::CommandBuffer& primaryCommandBuffer = frame.coordinator.commandBuffer();
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
        beginPrimaryRecording(primaryCommandBuffer, frameSlotIndex, imageIndex,
                              vk::RenderingFlagBits::eContentsSecondaryCommandBuffers);
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->secondaryCommandExecution};
        // Chunk order is preserved so recorded draw order survives the split.
        if (participants > 1)
        {
            const std::array secondaryCommands{*frame.secondaries.front().commandBuffer(),
                                               *frame.secondaries[1].commandBuffer()};
            primaryCommandBuffer.executeCommands(secondaryCommands);
        }
        else
        {
            const std::array secondaryCommands{*frame.secondaries.front().commandBuffer()};
            primaryCommandBuffer.executeCommands(secondaryCommands);
        }
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
        endPrimaryRecording(primaryCommandBuffer, imageIndex, captureAttempt);
    }
}

void Renderer::Impl::recordDirectCommands(std::size_t frameSlotIndex, std::uint32_t imageIndex,
                                          const detail::ForwardRecordingInput& input,
                                          const CaptureAttempt* captureAttempt,
                                          RendererCpuTimings* timings)
{
    CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
    const detail::FrameResources& frame = frames_[frameSlotIndex];
    const vk::raii::CommandBuffer& primaryCommandBuffer = frame.coordinator.commandBuffer();
    beginPrimaryRecording(primaryCommandBuffer, frameSlotIndex, imageIndex, {});
    detail::DrawBindingState bindingState = bindGeometryState(primaryCommandBuffer, input.state());
    recordDraws(primaryCommandBuffer, input.state(), input.draws(), std::move(bindingState));
    endPrimaryRecording(primaryCommandBuffer, imageIndex, captureAttempt);
}

void Renderer::Impl::beginPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer,
                                           std::size_t frameSlotIndex, std::uint32_t imageIndex,
                                           vk::RenderingFlags flags) const
{
    const vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBuffer.begin(beginInfo);
    transitionToAttachment(commandBuffer, imageIndex);
    transitionDepthToAttachment(commandBuffer, frameSlotIndex);
    beginGeometryPass(commandBuffer, frameSlotIndex, imageIndex, flags);
}

void Renderer::Impl::endPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer,
                                         std::uint32_t imageIndex,
                                         const CaptureAttempt* captureAttempt) const
{
    commandBuffer.endRendering();
    if (captureAttempt == nullptr)
    {
        transitionToPresent(commandBuffer, imageIndex);
    }
    else
    {
        recordCapture(commandBuffer, imageIndex, *captureAttempt);
    }
    commandBuffer.end();
}

void Renderer::Impl::transitionDepthToAttachment(const vk::raii::CommandBuffer& commandBuffer,
                                                 std::size_t frameSlotIndex) const
{
    // The depth value is cleared before every use, so no previous contents need
    // preserving. This slot's fence has completed before its command buffer is
    // recorded, making it safe to discard that slot's previous depth writes.
    const vk::ImageMemoryBarrier2 toAttachment{
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
        .image = presentation_->depthBuffer(frameSlotIndex).image(),
        .subresourceRange = detail::kDepthSubresourceRange,
    };
    const vk::DependencyInfo dependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toAttachment,
    };
    commandBuffer.pipelineBarrier2(dependency);
}

void Renderer::Impl::transitionToAttachment(const vk::raii::CommandBuffer& commandBuffer,
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
        .image = presentation_->swapchain().image(imageIndex),
        .subresourceRange = detail::kColorSubresourceRange,
    };
    const vk::DependencyInfo beginDependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toAttachment,
    };
    commandBuffer.pipelineBarrier2(beginDependency);
}

void Renderer::Impl::beginGeometryPass(const vk::raii::CommandBuffer& commandBuffer,
                                       std::size_t frameSlotIndex, std::uint32_t imageIndex,
                                       vk::RenderingFlags flags) const
{
    const vk::ClearValue clearValue{
        .color = {.float32 = std::array{0.015f, 0.02f, 0.03f, 1.0f}},
    };
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = *presentation_->swapchain().imageView(imageIndex),
        .imageLayout = vk::ImageLayout::eAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearValue,
    };
    const vk::ClearValue depthClear{
        .depthStencil = {.depth = 1.0f, .stencil = 0},
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = *presentation_->depthBuffer(frameSlotIndex).view(),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = depthClear,
    };
    const vk::RenderingInfo renderingInfo{
        .flags = flags,
        .renderArea =
            {
                .offset = {.x = 0, .y = 0},
                .extent = presentation_->swapchain().extent(),
            },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    commandBuffer.beginRendering(renderingInfo);
}

void Renderer::Impl::transitionToPresent(const vk::raii::CommandBuffer& commandBuffer,
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
        .image = presentation_->swapchain().image(imageIndex),
        .subresourceRange = detail::kColorSubresourceRange,
    };
    const vk::DependencyInfo endDependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toPresent,
    };
    commandBuffer.pipelineBarrier2(endDependency);
}

void Renderer::Impl::recordCapture(const vk::raii::CommandBuffer& commandBuffer,
                                   std::uint32_t imageIndex, const CaptureAttempt& attempt) const
{
    assert(readbackBuffer_ != nullptr);
    assert(attempt.extent == presentation_->swapchain().extent());
    assert(attempt.imageFormat == presentation_->swapchain().imageFormat());

    const vk::ImageMemoryBarrier2 toTransfer{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eAttachmentOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = presentation_->swapchain().image(imageIndex),
        .subresourceRange = detail::kColorSubresourceRange,
    };
    const vk::DependencyInfo transferDependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toTransfer,
    };
    commandBuffer.pipelineBarrier2(transferDependency);

    const vk::BufferImageCopy2 copyRegion{
        .bufferOffset = 0,
        // Zero selects tightly packed rows for image-to-buffer copies.
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {},
        .imageExtent = {.width = attempt.extent.width, .height = attempt.extent.height, .depth = 1},
    };
    const vk::CopyImageToBufferInfo2 copyInfo{
        .srcImage = presentation_->swapchain().image(imageIndex),
        .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
        .dstBuffer = readbackBuffer_->handle(),
        .regionCount = 1,
        .pRegions = &copyRegion,
    };
    commandBuffer.copyImageToBuffer2(copyInfo);

    const vk::ImageMemoryBarrier2 toPresent{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eNone,
        .dstAccessMask = vk::AccessFlagBits2::eNone,
        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = presentation_->swapchain().image(imageIndex),
        .subresourceRange = detail::kColorSubresourceRange,
    };
    const vk::DependencyInfo presentDependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toPresent,
    };
    commandBuffer.pipelineBarrier2(presentDependency);
}

CaptureAttempt Renderer::Impl::prepareCaptureAttempt()
{
    constexpr std::size_t kBytesPerPixel = 4;
    const detail::Swapchain& swapchain = presentation_->swapchain();
    const vk::Extent2D extent = swapchain.extent();
    const std::size_t width = extent.width;
    if (extent.width == 0 || extent.height == 0 ||
        width > std::numeric_limits<std::size_t>::max() / kBytesPerPixel)
    {
        throw std::runtime_error("The capture extent cannot be represented as RGBA8 rows");
    }
    const std::optional<detail::CaptureFormat> captureFormat =
        detail::captureFormatFor(swapchain.imageFormat());
    if (!captureFormat.has_value())
    {
        throw std::runtime_error("Frame capture requires an RGBA8 or BGRA8 sRGB swapchain format");
    }

    const std::size_t rowPitch = width * kBytesPerPixel;
    const std::size_t byteCount = detail::captureByteSize(extent.height, rowPitch);
    if (byteCount == std::numeric_limits<std::size_t>::max() ||
        byteCount > std::numeric_limits<vk::DeviceSize>::max())
    {
        throw std::runtime_error("The capture extent exceeds the readback address space");
    }
    if (readbackBuffer_ == nullptr || readbackBuffer_->size() != byteCount)
    {
        readbackBuffer_ = std::make_unique<detail::ReadbackBuffer>(allocator_, byteCount);
    }
    return {
        .extent = extent,
        .imageFormat = swapchain.imageFormat(),
        .captureFormat = *captureFormat,
        .rowPitch = rowPitch,
        .byteCount = byteCount,
    };
}

void Renderer::Impl::waitForCaptureSubmission(const detail::FrameSlot& frameSlot) const
{
    const vk::Result result = device_.logicalDevice().waitForFences(
        *frameSlot.frameFinished(), vk::True, std::numeric_limits<std::uint64_t>::max());
    if (result != vk::Result::eSuccess)
    {
        throw vk::SystemError{vk::make_error_code(result), "Waiting for frame capture completion"};
    }
}

void Renderer::Impl::commitCapture(const CaptureAttempt& attempt)
{
    assert(captureRequest_.has_value());
    assert(readbackBuffer_ != nullptr);
    assert(presentedFrameCount_ == captureRequest_->frameOrdinal);

    const std::span<const std::byte> mapped = readbackBuffer_->bytes(attempt.byteCount);
    const std::vector<std::uint8_t> rgba =
        detail::toRgba8(mapped, attempt.extent.width, attempt.extent.height, attempt.rowPitch,
                        attempt.captureFormat);
    if (rgba.empty())
    {
        throw std::runtime_error("The captured swapchain image could not be converted to RGBA8");
    }
    if (!detail::writeRgba8Png(captureRequest_->outputPath, rgba, attempt.extent.width,
                               attempt.extent.height))
    {
        throw std::runtime_error("Writing the frame capture failed for '" +
                                 captureRequest_->outputPath.string() + "'");
    }

    std::error_code fileError;
    const bool isRegularFile =
        std::filesystem::is_regular_file(captureRequest_->outputPath, fileError);
    const std::uintmax_t fileSize =
        isRegularFile ? std::filesystem::file_size(captureRequest_->outputPath, fileError) : 0;
    if (fileError || !isRegularFile || fileSize == 0)
    {
        throw std::runtime_error("The frame capture did not produce a non-empty regular file at '" +
                                 captureRequest_->outputPath.string() + "'");
    }

    fire_engine::log("Captured frame {} to {} ({}x{}, {}, {} presentation recreations).",
                     captureRequest_->frameOrdinal, captureRequest_->outputPath.string(),
                     attempt.extent.width, attempt.extent.height,
                     vk::to_string(attempt.imageFormat), presentationRecreationCount_);
    captureComplete_ = true;
}

namespace
{
/* --- File-local functions --- */

std::optional<FrameCaptureRequest>
validatedCaptureRequest(std::optional<FrameCaptureRequest> request)
{
    if (request.has_value() && (request->outputPath.empty() || request->frameOrdinal == 0))
    {
        throw std::invalid_argument(
            "A frame capture requires a non-empty path and positive frame ordinal");
    }
    return request;
}

/**
 * @brief Maps positive normalized-device Y upward in framebuffer space.
 * @param extent Framebuffer dimensions covered by the viewport.
 * @return Full-extent viewport with the framebuffer Y inversion applied.
 */
[[nodiscard]] constexpr vk::Viewport sceneViewport(vk::Extent2D extent) noexcept
{
    // Vulkan's framebuffer Y points down. A negative-height viewport flips it back,
    // which keeps Mat4::perspective a conventional Y-up projection. The origin moves
    // to the bottom edge, hence y = height with a negative height. Negative viewport
    // height is core since Vulkan 1.1 (VK_KHR_maintenance1).
    return {
        .x = 0.0f,
        .y = static_cast<float>(extent.height),
        .width = static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
}

static_assert(sceneViewport(vk::Extent2D{.width = 800, .height = 600}).height == -600.0f);
static_assert(sceneViewport(vk::Extent2D{.width = 800, .height = 600}).y == 600.0f);

constexpr std::size_t automaticParticipantCount(std::size_t drawCount) noexcept
{
    const std::size_t supported = drawCount / kMinimumDrawsPerRecordingParticipant;
    if (supported < 2)
    {
        return 1;
    }
    return supported > kMaxSecondaryRecordingThreads ? kMaxSecondaryRecordingThreads : supported;
}

static_assert(automaticParticipantCount(0) == 1);
static_assert(automaticParticipantCount(1) == 1);
static_assert(automaticParticipantCount(1000) == 1);
// One participant below the measured boundary, two at it.
static_assert(automaticParticipantCount(kMinimumDrawsPerRecordingParticipant * 2 - 1) == 1);
static_assert(automaticParticipantCount(kMinimumDrawsPerRecordingParticipant * 2) == 2);
// Never above the supported maximum, however large the workload.
static_assert(automaticParticipantCount(kMinimumDrawsPerRecordingParticipant * 100) ==
              kMaxSecondaryRecordingThreads);

detail::DrawBindingState bindGeometryState(const vk::raii::CommandBuffer& commandBuffer,
                                           const detail::ForwardRecordingState& state)
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, state.pipeline);
    commandBuffer.setViewport(0, state.viewport);
    commandBuffer.setScissor(0, state.scissor);

    const vk::DescriptorBufferInfo uniformInfo{
        .buffer = state.frameUniformBuffer,
        .offset = 0,
        .range = sizeof(detail::FrameUniforms),
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

void recordDraws(const vk::raii::CommandBuffer& commandBuffer,
                 const detail::ForwardRecordingState& state,
                 std::span<const detail::ForwardRecordingDraw> draws,
                 detail::DrawBindingState bindingState)
{
    constexpr vk::DeviceSize bufferOffset = 0;
    for (const detail::ForwardRecordingDraw& draw : draws)
    {
        const detail::DrawBindingChanges changes =
            bindingState.update(draw.vertexBuffer, draw.indexBuffer, draw.sampler, draw.imageView);
        if (changes.geometry)
        {
            commandBuffer.bindVertexBuffers(0, draw.vertexBuffer, bufferOffset);
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

        commandBuffer.pushConstants<detail::DrawConstants>(
            state.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, draw.constants);
        commandBuffer.drawIndexed(draw.indexCount, 1, 0, 0, 0);
    }
}

void recordForwardSecondaryChunk(const detail::ForwardSecondaryChunkJob& job,
                                 detail::ChunkRecordingTimings* timings)
{
    // Each participant resets its own pool as the first act of its own work,
    // which is the registered ownership rule for worker-local reset cost.
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
    detail::DrawBindingState bindingState = bindGeometryState(commandBuffer, job.state);
    recordDraws(commandBuffer, job.state, job.draws, std::move(bindingState));
    commandBuffer.end();

    if (timings != nullptr)
    {
        timings->recordEnd = std::chrono::steady_clock::now();
        timings->recorded = true;
    }
}

void mergeChunkTimings(
    const std::array<detail::ChunkRecordingTimings, kMaxSecondaryRecordingThreads>& chunkTimings,
    std::size_t participants, const detail::CompletionWait* completionWait,
    RendererCpuTimings* timings)
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
        const detail::ChunkRecordingTimings& chunk = chunkTimings[index];
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
        const detail::ChunkRecordingTimings& chunk = chunkTimings[index];
        const auto poolReset =
            std::chrono::duration_cast<std::chrono::nanoseconds>(chunk.resetEnd - chunk.resetStart);
        const auto recording =
            std::chrono::duration_cast<std::chrono::nanoseconds>(chunk.recordEnd - chunk.resetEnd);
        timings->chunks[index] = ChunkCpuTimings{
            .poolReset = poolReset,
            .recording = recording,
            .resetStartOffset = std::chrono::duration_cast<std::chrono::nanoseconds>(
                chunk.resetStart - earliestStart),
            .recorded = true,
        };
        timings->workerCommandPoolReset += poolReset;
        timings->secondaryCommandRecording += recording;
    }

    // The reset-region span next to the participant reset durations is what
    // decides whether concurrent resets overlapped or serialized.
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

} // namespace
/** @endcond */

} // namespace fire_engine
