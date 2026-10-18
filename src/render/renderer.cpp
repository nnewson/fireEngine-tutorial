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
#include <fire_engine/render/detail/cpu_phase_timer.hpp>
#include <fire_engine/render/detail/depth_buffer.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/forward_recorder.hpp>
#include <fire_engine/render/detail/frame_barriers.hpp>
#include <fire_engine/render/detail/frame_recording_input.hpp>
#include <fire_engine/render/detail/frame_resources.hpp>
#include <fire_engine/render/detail/frame_slot.hpp>
#include <fire_engine/render/detail/frame_submission.hpp>
#include <fire_engine/render/detail/presentation_state.hpp>
#include <fire_engine/render/detail/readback_buffer.hpp>
#include <fire_engine/render/detail/recording_context.hpp>
#include <fire_engine/render/detail/resource_compiler.hpp>
#include <fire_engine/render/detail/shadow_map_policy.hpp>
#include <fire_engine/render/detail/shadow_pipeline.hpp>
#include <fire_engine/render/detail/shadow_recorder.hpp>
#include <fire_engine/render/detail/submission_serials.hpp>
#include <fire_engine/render/detail/swapchain.hpp>
#include <fire_engine/scene/scene_draw_list.hpp>

#include <array>
#include <cassert>
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
using detail::CpuPhaseTimer;

/** @brief One mesh-layout requirement shared by preparation and both pipelines. */
constexpr PipelineDescription kPipelineDescription{};

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

/**
 * @brief Creates the renderer-owned timeline semaphore that retires frame submissions.
 * @param device Logical device with timeline semaphores enabled.
 * @param initialValue Counter value matching the submission ledger's starting serial.
 * @return Timeline semaphore whose counter starts at initialValue.
 * @throws vk::SystemError if Vulkan cannot create the semaphore.
 */
[[nodiscard]] vk::raii::Semaphore createFrameTimeline(const detail::Device& device,
                                                      std::uint64_t initialValue);

[[nodiscard]] constexpr vk::Viewport sceneViewport(vk::Extent2D extent) noexcept;
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
     * @param configuration Fixed forward-recording and capture choices.
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
     * @param frameDescription Application-owned camera and shadow values sampled for this frame.
     * @param timings Optional output populated with host timings for this attempt.
     * @return Presentation outcome for the acquired swapchain image.
     */
    [[nodiscard]] RenderResult drawFrame(const SceneDrawList& drawList,
                                         const FrameDescription& frameDescription,
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
     * @brief Begins one reset primary command buffer before pass recording.
     * @param commandBuffer Selected frame-slot primary command buffer.
     */
    static void beginPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer);

    /**
     * @brief Records frame-output policy and ends one primary command buffer.
     * @param commandBuffer Primary command buffer receiving the frame suffix.
     * @param imageIndex Acquired swapchain-image index transitioned for presentation.
     * @param captureAttempt Selected readback copy, or null for an attachment-to-present suffix.
     */
    void endPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer, std::uint32_t imageIndex,
                             const CaptureAttempt* captureAttempt) const;

    /** @brief Reports whether any submission slot may still be in use. @return Pending state. */
    [[nodiscard]] bool workMayBePending() const noexcept;

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
     * @brief Waits on the host until the frame timeline reaches one submission serial.
     * @param serial Serial signaled by a successful frame submission.
     * @return Vulkan's wait result; anything but success authorizes no reuse.
     */
    [[nodiscard]] vk::Result waitForSubmission(std::uint64_t serial) const;

    /**
     * @brief Waits for the capture submission before host access to its readback bytes.
     * @param serial Serial signaled by the submission that recorded the capture copy.
     */
    void waitForCaptureSubmission(std::uint64_t serial) const;

    /**
     * @brief Invalidates, converts, and writes one successfully presented capture.
     * @param attempt Extent and format snapshot used when the copy was recorded.
     */
    void commitCapture(const CaptureAttempt& attempt);

    // Reverse destruction keeps every allocation ahead of its VMA and Vulkan
    // owners. Presentation lifetime retains the separate Swapchain precondition.

    // Foundational long-lived state.
    std::optional<FrameCaptureRequest> captureRequest_; ///< Owned one-shot diagnostic request.
    detail::Device device_;                     ///< Vulkan instance, surface, device, and queues.
    detail::MemoryAllocator allocator_;         ///< VMA owner created from the logical device.
    detail::ResourceCompiler resourceCompiler_; ///< Dedicated setup-time upload context.
    std::unique_ptr<detail::ReadbackBuffer> readbackBuffer_; ///< Lazy one-shot capture storage.

    // Renderer-lifetime shadow policy and shared sampling state. The tracker
    // precedes every owner whose successful construction it observes.
    detail::ShadowMapCreationTracker shadowMapCreationTracker_; ///< Monotonic local count.
    vk::Format shadowMapFormat_; ///< Sampled depth format selected once for every slot.
    vk::raii::Sampler shadowComparisonSampler_; ///< Comparison state shared by all slots.
    detail::ShadowPipeline shadowPipeline_;     ///< Renderer-lifetime depth-only pipeline.

    // Presentation-dependent state replaced as one ownership group.
    std::unique_ptr<detail::PresentationState> presentation_; ///< Swapchain-compatible resources.

    std::array<detail::FrameResources, detail::kFrameSlotCount>
        frames_; ///< Presentation-independent slot state.
    // The ledger precedes the timeline, which is created at the ledger's value.
    // Neither belongs to PresentationState, so recreation never resets them.
    detail::SubmissionSerials submissionSerials_; ///< Published serials and the selected slot.
    vk::raii::Semaphore frameTimeline_;     ///< Signaled once per successful frame submission.
    std::uint64_t presentedFrameCount_ = 0; ///< Successful presentations seen by capture logic.
    std::uint64_t presentationRecreationCount_ = 0; ///< Completed presentation replacements.
    bool captureComplete_ = false; ///< Whether the configured PNG committed successfully.

    // Prepared state compiled from the current scene dependencies.
    RenderPreparation renderPreparation_;           ///< Vulkan-free validation and plan cache.
    detail::CompiledResources compiledResources_;   ///< Installed GPU graph and its last use.
    std::optional<std::size_t> compiledGeneration_; ///< Plan generation uploaded to the GPU.
    detail::FrameRecordingInputCompiler
        frameRecordingInputCompiler_;       ///< Reusable pass-specific packet-freeze arenas.
    detail::ShadowRecorder shadowRecorder_; ///< Serial depth-only recording boundary.

    // Declared last so reverse member destruction stops its helper before the
    // recording contexts whose pools it writes into.
    detail::ForwardRecorder forwardRecorder_; ///< Concrete forward-pass recording boundary.
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

RenderResult Renderer::drawFrame(const SceneDrawList& drawList,
                                 const FrameDescription& frameDescription,
                                 RendererCpuTimings* timings)
{
    return implementation_->drawFrame(drawList, frameDescription, timings);
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
    // Validate the Vulkan-free request before constructing the device and
    // capture-enabled swapchain.
    : captureRequest_{validatedCaptureRequest(std::move(configuration.captureRequest))},
      device_{glfw, window, applicationName},
      allocator_{device_},
      resourceCompiler_{device_, allocator_},
      shadowMapFormat_{detail::queryShadowMapFormat(device_)},
      shadowComparisonSampler_{device_.logicalDevice(),
                               detail::shadowComparisonSamplerCreateInfo()},
      shadowPipeline_{device_, kPipelineDescription, shadowMapFormat_},
      presentation_{std::make_unique<detail::PresentationState>(
          device_, allocator_, kPipelineDescription, window.framebufferExtent(),
          captureRequest_.has_value())},
      // Frame resources are presentation-independent. Synchronization,
      // recording contexts, and shadow views borrow the device; uniform and
      // shadow-image allocations borrow the allocator. Both owners precede them.
      frames_{
          detail::FrameResources{
              .slot = detail::FrameSlot{device_},
              .uniforms = detail::FrameUniformBuffer{allocator_},
              .shadow =
                  {
                      .map = detail::ShadowMap{device_, allocator_, shadowMapFormat_,
                                               shadowMapCreationTracker_},
                      .primary =
                          detail::RecordingContext{device_, detail::RecordingBufferKind::ePrimary},
                  },
              .forward =
                  {
                      .primary =
                          detail::RecordingContext{device_, detail::RecordingBufferKind::ePrimary},
                      .participants = {detail::RecordingContext{
                                           device_, detail::ForwardRecorder::secondaryBufferKind(
                                                        configuration.forwardRecordingMode)},
                                       detail::RecordingContext{
                                           device_, detail::ForwardRecorder::secondaryBufferKind(
                                                        configuration.forwardRecordingMode)}},
                  },
          },
          detail::FrameResources{
              .slot = detail::FrameSlot{device_},
              .uniforms = detail::FrameUniformBuffer{allocator_},
              .shadow =
                  {
                      .map = detail::ShadowMap{device_, allocator_, shadowMapFormat_,
                                               shadowMapCreationTracker_},
                      .primary =
                          detail::RecordingContext{device_, detail::RecordingBufferKind::ePrimary},
                  },
              .forward =
                  {
                      .primary =
                          detail::RecordingContext{device_, detail::RecordingBufferKind::ePrimary},
                      .participants = {detail::RecordingContext{
                                           device_, detail::ForwardRecorder::secondaryBufferKind(
                                                        configuration.forwardRecordingMode)},
                                       detail::RecordingContext{
                                           device_, detail::ForwardRecorder::secondaryBufferKind(
                                                        configuration.forwardRecordingMode)}},
                  },
          }},
      frameTimeline_{createFrameTimeline(device_, submissionSerials_.lastSubmitted())},
      forwardRecorder_{configuration.forwardRecordingMode,
                       configuration.forcedForwardRecordingParticipantCount}
{
    if (!*device_.graphicsQueue() || !*device_.presentQueue())
    {
        throw std::runtime_error("Vulkan returned a null device queue");
    }
    if (allocator_.handle() == nullptr)
    {
        throw std::runtime_error("VMA returned a null allocator");
    }
    // Each slot's first use waits for nothing, so retirement depends on the
    // counter starting exactly where the ledger's serials begin.
    if (frameTimeline_.getCounterValue() != submissionSerials_.lastSubmitted())
    {
        throw std::runtime_error("The frame timeline did not start at its initial value");
    }
}

Renderer::Impl::~Impl() noexcept
{
    // drawFrame() waits for the helper synchronously, so every entry point
    // below begins with it idle. Assert the invariant rather than joining,
    // which would defeat the helper's persistence.
    assert(forwardRecorder_.idle());
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
    assert(forwardRecorder_.idle());
    // Planning validates every CPU relationship before the first Vulkan
    // allocation, keeping malformed input failures deterministic and cheap.
    const RenderPreparationPlan& plan =
        renderPreparation_.build(assets, drawList, kPipelineDescription);
    if (compiledGeneration_.has_value() && *compiledGeneration_ == renderPreparation_.generation())
    {
        return;
    }

    // The installed graph stays usable while its replacement compiles. A
    // failure here or in the wait below leaves the installed graph, its last
    // use and the compiled generation unchanged.
    std::unique_ptr<detail::CompiledResourceGraph> candidate =
        resourceCompiler_.compile(assets, plan, compiledResources_.graph());

    // Only recorded commands use compiled resources, and the all-commands
    // timeline signal for their last use also covers every earlier
    // submission. Presentation never uses them, so its retirement stays with
    // waitIdle() and the destructor. A graph no submission used needs no wait.
    if (const std::optional<std::uint64_t> lastUse = compiledResources_.lastUse())
    {
        const vk::Result retirementResult = waitForSubmission(*lastUse);
        if (retirementResult != vk::Result::eSuccess)
        {
            throw vk::SystemError{vk::make_error_code(retirementResult),
                                  "Waiting for compiled-resource retirement"};
        }
    }
    compiledResources_.replace(std::move(candidate));
    compiledGeneration_ = renderPreparation_.generation();
}

RenderResult Renderer::Impl::drawFrame(const SceneDrawList& drawList,
                                       const FrameDescription& frameDescription,
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
    const std::size_t frameSlotIndex = submissionSerials_.slot();
    detail::FrameResources& frame = frames_[frameSlotIndex];
    detail::FrameSlot& frameSlot = frame.slot;
    // Freeze external IDs and transforms before acquisition. A compiler
    // failure therefore cannot abandon a signaled acquisition semaphore.
    const detail::FrameRecordingInput frameRecordingInput = [&]() -> detail::FrameRecordingInput
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.recordingInputBuild};
        const vk::Extent2D extent = presentation_->swapchain().extent();
        const detail::FrameUniforms frameUniforms{
            .viewProjection = cameraViewProjection(frameDescription.camera,
                                                   static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height)),
            .lightViewProjection =
                directionalShadowViewProjection(frameDescription.directionalShadow),
        };
        const detail::ShadowRecordingState shadowRecordingState{
            .pipeline = *shadowPipeline_.pipeline(),
            .pipelineLayout = *shadowPipeline_.pipelineLayout(),
            .frameUniformBuffer = frame.uniforms.handle(),
            .depthAttachmentFormat = shadowMapFormat_,
            .vertexLayout = kPipelineDescription.vertexLayout,
        };
        const detail::ForwardRecordingState forwardRecordingState{
            .pipeline = *presentation_->forwardPipeline().pipeline(),
            .pipelineLayout = *presentation_->forwardPipeline().pipelineLayout(),
            .frameUniformBuffer = frame.uniforms.handle(),
            .shadowMapView = *frame.shadow.map.view(),
            .shadowComparisonSampler = *shadowComparisonSampler_,
            .viewport = sceneViewport(extent),
            .scissor = {.offset = {.x = 0, .y = 0}, .extent = extent},
            .colorAttachmentFormat = presentation_->swapchain().imageFormat(),
            .depthAttachmentFormat = presentation_->depthBuffer(frameSlotIndex).format(),
            .vertexLayout = kPipelineDescription.vertexLayout,
        };
        return frameRecordingInputCompiler_.compile(drawList, compiledResources_.view(),
                                                    frameUniforms, shadowRecordingState,
                                                    forwardRecordingState);
    }();

    // Choosing the serial reserves nothing; exhaustion is rejected here, before
    // acquisition and before this slot's state is touched.
    const std::uint64_t submissionSerial = submissionSerials_.candidate();
    vk::Result retirementResult = vk::Result::eSuccess;
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.frameFenceWait};
        // A slot's first use waits for nothing. Later uses wait for the serial of
        // that slot's own last successful submission.
        if (const std::optional<std::uint64_t> retirementValue =
                submissionSerials_.retirementValue())
        {
            retirementResult = waitForSubmission(*retirementValue);
        }
    }
    if (retirementResult != vk::Result::eSuccess)
    {
        throw vk::SystemError{vk::make_error_code(retirementResult),
                              "Waiting for frame-slot retirement"};
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.frameUniformUpdate};
        frame.uniforms.update(frameRecordingInput.frameUniforms());
    }

    std::uint32_t imageIndex = 0;
    bool swapchainIsSuboptimal = false;
    try
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.imageAcquisitionWait};
        const auto [result, acquiredImageIndex] =
            presentation_->swapchain().handle().acquireNextImage(
                std::numeric_limits<std::uint64_t>::max(), *frameSlot.imageAvailable());
        imageIndex = acquiredImageIndex;
        swapchainIsSuboptimal = result == vk::Result::eSuboptimalKHR;
    }
    catch (const vk::OutOfDateKHRError&)
    {
        // Nothing was submitted, so no serial is published and the same slot
        // is selected again with its retirement value unchanged.
        return RenderResult::eNotPresented;
    }

    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.presentationFenceWait};
        presentation_->preparePresentFence(imageIndex);
    }
    const detail::ShadowPassTarget shadowTarget{
        .depthImage = frame.shadow.map.image(),
        .depthView = *frame.shadow.map.view(),
        .depthFormat = frame.shadow.map.format(),
        .extent = detail::kShadowMapExtent,
    };
    shadowRecorder_.record(frame.shadow.primary, frameRecordingInput.shadow(), shadowTarget,
                           timings == nullptr ? nullptr : &timings->shadow);
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr
                                               : &timings->forward.coordinatorCommandPoolReset};
        frame.forward.primary.resetCommands();
    }
    const vk::raii::CommandBuffer& primaryCommandBuffer = frame.forward.primary.commandBuffer();
    detail::ForwardPassTarget forwardTarget{};
    {
        // This begins before helper dispatch after the extraction. All command
        // work and timing membership remain unchanged; only that host order moves.
        CpuPhaseTimer timer{timings == nullptr ? nullptr
                                               : &timings->forward.primaryCommandRecording};
        beginPrimaryRecording(primaryCommandBuffer);
        forwardTarget = {
            .colorImage = presentation_->swapchain().image(imageIndex),
            .colorView = *presentation_->swapchain().imageView(imageIndex),
            .colorFormat = presentation_->swapchain().imageFormat(),
            .depthImage = presentation_->depthBuffer(frameSlotIndex).image(),
            .depthView = *presentation_->depthBuffer(frameSlotIndex).view(),
            .depthFormat = presentation_->depthBuffer(frameSlotIndex).format(),
            .extent = presentation_->swapchain().extent(),
        };
    }
    forwardRecorder_.record(frame.forward.primary, std::span{frame.forward.participants},
                            frameRecordingInput.forward(), forwardTarget,
                            timings == nullptr ? nullptr : &timings->forward);
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr
                                               : &timings->forward.primaryCommandRecording};
        endPrimaryRecording(primaryCommandBuffer, imageIndex,
                            captureAttempt.has_value() ? &*captureAttempt : nullptr);
    }
    // A failed submission publishes nothing: the slot stays selected and keeps
    // waiting for its previous serial, which an earlier submission signals.
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.queueSubmission};

        const vk::SemaphoreSubmitInfo waitInfo{
            .semaphore = *frameSlot.imageAvailable(),
            .stageMask = detail::kAcquireWaitStage,
        };
        // Publication at the end of the shadow primary precedes the forward
        // primary in the same submission. One timeline serial retires both.
        const std::array commandInfos{
            vk::CommandBufferSubmitInfo{.commandBuffer = *frame.shadow.primary.commandBuffer()},
            vk::CommandBufferSubmitInfo{.commandBuffer = *frame.forward.primary.commandBuffer()},
        };
        // Capture signals no render-finished semaphore; see
        // detail::frameSignalSemaphoreInfos().
        const detail::FrameSignals signals = detail::frameSignalSemaphoreInfos(
            *frameTimeline_, submissionSerial,
            *presentation_->swapchain().renderFinished(imageIndex), captureAttempt.has_value());
        const vk::SubmitInfo2 submitInfo{
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &waitInfo,
            .commandBufferInfoCount = static_cast<std::uint32_t>(commandInfos.size()),
            .pCommandBufferInfos = commandInfos.data(),
            .signalSemaphoreInfoCount = signals.count,
            .pSignalSemaphoreInfos = signals.infos.data(),
        };
        device_.graphicsQueue().submit2(submitInfo);
    }
    // The submission succeeded, so this is the commit point. None of these
    // calls throws, and a later capture or presentation failure leaves all
    // three in place.
    submissionSerials_.commit(submissionSerial);
    frameSlot.markWorkPending();
    compiledResources_.markUsed(submissionSerial);
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
        waitForCaptureSubmission(submissionSerial);
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
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.presentation};
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
    assert(forwardRecorder_.idle());
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

bool Renderer::Impl::recreatePresentation(FramebufferExtent framebufferExtent)
{
    assert(forwardRecorder_.idle());
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
        device_, allocator_, kPipelineDescription, framebufferExtent, captureRequest_.has_value(),
        oldSwapchain);
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
        .forwardDepthFormat = vk::to_string(presentation_->depthBuffer(0).format()),
        .shadowMapFormat = vk::to_string(shadowMapFormat_),
        .shadowMapWidth = detail::kShadowMapExtent.width,
        .shadowMapHeight = detail::kShadowMapExtent.height,
        .shadowMapCount = frames_.size(),
        .shadowMapCreationCount = shadowMapCreationTracker_.count(),
        .presentMode = vk::to_string(presentation_->swapchain().presentMode()),
        .forwardRecordingMode = forwardRecorder_.mode(),
        .forcedForwardRecordingParticipantCount = forwardRecorder_.forcedParticipantCount(),
        .minimumDrawsPerForwardRecordingParticipant = forwardRecorder_.minimumDrawsPerParticipant(),
    };
}

void Renderer::Impl::beginPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer)
{
    const vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBuffer.begin(beginInfo);
}

void Renderer::Impl::endPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer,
                                         std::uint32_t imageIndex,
                                         const CaptureAttempt* captureAttempt) const
{
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

void Renderer::Impl::transitionToPresent(const vk::raii::CommandBuffer& commandBuffer,
                                         std::uint32_t imageIndex) const
{
    // Chains into the render-finished signal; see detail::presentationBarrier().
    const vk::ImageMemoryBarrier2 toPresent =
        detail::presentationBarrier(presentation_->swapchain().image(imageIndex));
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

    // Its new layout must match copyInfo.srcImageLayout below.
    const vk::ImageMemoryBarrier2 toTransfer =
        detail::captureCopySourceBarrier(presentation_->swapchain().image(imageIndex));
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

    // The empty destination relies on capture's host wait; see
    // detail::capturePresentationBarrier().
    const vk::ImageMemoryBarrier2 toPresent =
        detail::capturePresentationBarrier(presentation_->swapchain().image(imageIndex));
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

vk::Result Renderer::Impl::waitForSubmission(std::uint64_t serial) const
{
    const vk::Semaphore timeline = *frameTimeline_;
    const vk::SemaphoreWaitInfo waitInfo{
        .semaphoreCount = 1,
        .pSemaphores = &timeline,
        .pValues = &serial,
    };
    return device_.logicalDevice().waitSemaphores(waitInfo,
                                                  std::numeric_limits<std::uint64_t>::max());
}

void Renderer::Impl::waitForCaptureSubmission(std::uint64_t serial) const
{
    const vk::Result result = waitForSubmission(serial);
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

vk::raii::Semaphore createFrameTimeline(const detail::Device& device, std::uint64_t initialValue)
{
    const vk::SemaphoreTypeCreateInfo typeInfo{
        .semaphoreType = vk::SemaphoreType::eTimeline,
        .initialValue = initialValue,
    };
    const vk::SemaphoreCreateInfo createInfo{
        .pNext = &typeInfo,
    };
    return {device.logicalDevice(), createInfo};
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

} // namespace
/** @endcond */

} // namespace fire_engine
