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
#include <fire_engine/render/detail/forward_recording_input.hpp>
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
    // Validate the Vulkan-free request before constructing the device and
    // capture-enabled swapchain.
    : captureRequest_{validatedCaptureRequest(std::move(configuration.captureRequest))},
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
                  .secondaries = {detail::RecordingContext{
                                      device_, detail::ForwardRecorder::secondaryBufferKind(
                                                   configuration.forwardRecordingMode)},
                                  detail::RecordingContext{
                                      device_, detail::ForwardRecorder::secondaryBufferKind(
                                                   configuration.forwardRecordingMode)}}},
              detail::FrameResources{
                  .slot = detail::FrameSlot{device_},
                  .forwardUniforms = detail::ForwardFrameUniformBuffer{allocator_},
                  .coordinator =
                      detail::RecordingContext{device_, detail::RecordingBufferKind::ePrimary},
                  .secondaries = {detail::RecordingContext{
                                      device_, detail::ForwardRecorder::secondaryBufferKind(
                                                   configuration.forwardRecordingMode)},
                                  detail::RecordingContext{
                                      device_, detail::ForwardRecorder::secondaryBufferKind(
                                                   configuration.forwardRecordingMode)}}}},
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
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->forward.recordingInputBuild};
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
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.frameFenceWait};
        fenceResult = logicalDevice.waitForFences(*frameSlot.frameFinished(), vk::True,
                                                  std::numeric_limits<std::uint64_t>::max());
    }
    if (fenceResult != vk::Result::eSuccess)
    {
        throw vk::SystemError{vk::make_error_code(fenceResult), "Waiting for the frame fence"};
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->forward.frameUniformUpdate};
        frame.forwardUniforms.update(forwardRecordingInput.state().frameUniforms);
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
        // The fence is still signaled because acquisition happens before
        // its reset. Do not advance the cycle when this slot submitted nothing.
        return RenderResult::eNotPresented;
    }

    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.presentationFenceWait};
        presentation_->preparePresentFence(imageIndex);
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr
                                               : &timings->forward.coordinatorCommandPoolReset};
        frame.coordinator.resetCommands();
    }
    const vk::raii::CommandBuffer& primaryCommandBuffer = frame.coordinator.commandBuffer();
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
    forwardRecorder_.record(frame.coordinator, std::span{frame.secondaries}, forwardRecordingInput,
                            forwardTarget, timings == nullptr ? nullptr : &timings->forward);
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr
                                               : &timings->forward.primaryCommandRecording};
        endPrimaryRecording(primaryCommandBuffer, imageIndex,
                            captureAttempt.has_value() ? &*captureAttempt : nullptr);
    }
    // Nothing intentionally abandons the frame after this reset: a
    // successful submission will signal the fence, while errors unwind.
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->common.queueSubmission};
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

} // namespace
/** @endcond */

} // namespace fire_engine
