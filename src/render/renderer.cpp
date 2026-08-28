#include <fire_engine/render/renderer.hpp>

#include <fire_engine/core/log.hpp>
#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/graphics/render_preparation.hpp>
#include <fire_engine/math/normalize_error.hpp>
#include <fire_engine/math/vec3.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/compiled_resource_graph.hpp>
#include <fire_engine/render/detail/compiled_resources.hpp>
#include <fire_engine/render/detail/depth_buffer.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/draw_binding_state.hpp>
#include <fire_engine/render/detail/draw_constants.hpp>
#include <fire_engine/render/detail/frame_slot.hpp>
#include <fire_engine/render/detail/image_subresource_ranges.hpp>
#include <fire_engine/render/detail/pipeline.hpp>
#include <fire_engine/render/detail/recording_context.hpp>
#include <fire_engine/render/detail/resource_compiler.hpp>
#include <fire_engine/render/detail/swapchain.hpp>
#include <fire_engine/scene/scene.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fire_engine
{
namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

/** @brief Vulkan-free mesh layout required by the tutorial scene pipeline. */
constexpr PipelineDescription kScenePipelineDescription{};

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

/** @brief Replaceable swapchain, attachments, pipeline, and presentation completion state. */
class PresentationState final
{
public:
    /**
     * @brief Creates one complete set of mutually compatible presentation resources.
     * @param device Device and queues used for rendering and presentation.
     * @param allocator VMA owner used for the depth attachment.
     * @param window Window whose current framebuffer determines the extent.
     * @param oldSwapchain Previous swapchain offered for implementation reuse.
     */
    PresentationState(const detail::Device& device, const detail::MemoryAllocator& allocator,
                      const Window& window, vk::SwapchainKHR oldSwapchain = nullptr);

    /** @brief Returns the owned swapchain. @return Presentation images and semaphores. */
    [[nodiscard]] const detail::Swapchain& swapchain() const noexcept;
    /** @brief Returns the extent-matched depth attachment. @return Owned depth state. */
    [[nodiscard]] const detail::DepthBuffer& depthBuffer() const noexcept;
    /** @brief Returns the attachment-compatible graphics pipeline. @return Owned pipeline. */
    [[nodiscard]] const detail::Pipeline& pipeline() const noexcept;

    /**
     * @brief Waits and resets an earlier presentation fence before its image reuses it.
     * @param imageIndex Newly acquired swapchain-image index.
     */
    void preparePresentFence(std::size_t imageIndex);

    /**
     * @brief Returns the unsignaled fence associated with the next present of one image.
     * @param imageIndex Acquired swapchain-image index.
     * @return Fence chained to VkPresentInfoKHR.
     */
    [[nodiscard]] const vk::raii::Fence& presentFence(std::size_t imageIndex) const;

    /**
     * @brief Records that presentation will signal one image's fence.
     * @param imageIndex Presented swapchain-image index.
     */
    void markPresentSubmitted(std::size_t imageIndex);

    /** @brief Waits until all submitted presentation resources may be destroyed. */
    void waitForPresentations();

private:
    // Reverse destruction releases fences and the pipeline before attachment
    // resources, and releases the depth allocation before the swapchain.
    const vk::raii::Device* logicalDevice_ = nullptr; ///< Borrowed owner used for fence waits.
    detail::Swapchain swapchain_;     ///< Images, views, and per-image binary semaphores.
    detail::DepthBuffer depthBuffer_; ///< Extent-matched depth image and view.
    detail::Pipeline pipeline_;       ///< Compatible color/depth graphics pipeline.
    std::vector<vk::raii::Fence> presentFences_; ///< Completion fence per image.
    std::vector<std::uint8_t> presentSubmitted_; ///< Whether each fence has pending work.
};

/* --- File-local function declarations --- */

[[nodiscard]] Mat4 createViewProjection(vk::Extent2D extent);
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
     * @param scene Scene whose draw dependencies select the compiled subset.
     */
    void prepare(const RenderAssets& assets, const Scene& scene);

    /**
     * @brief Records, submits, and presents the current scene once.
     * @param scene Prepared scene supplying current draw items and transforms.
     * @param timings Optional output populated with host timings for this attempt.
     * @return Presentation outcome for the acquired swapchain image.
     */
    [[nodiscard]] RenderResult drawFrame(const Scene& scene, RendererCpuTimings* timings);

    /**
     * @brief Replaces the complete presentation-dependent ownership group.
     * @param window Window whose current framebuffer selects the new extent.
     * @return true after replacement, or false for a transient zero-sized framebuffer.
     */
    [[nodiscard]] bool recreatePresentation(const Window& window);

    /** @brief Waits for device work and clears pending-submission bookkeeping. */
    void waitIdle();

    /** @brief Describes the selected Vulkan and presentation state. @return Public summary. */
    [[nodiscard]] RendererInfo info() const;

private:
    /**
     * @brief Records the complete command-buffer sequence for one acquired image.
     * @param imageIndex Acquired swapchain-image index.
     * @param drawItems Current ordered scene draws.
     * @param timings Optional output receiving the serial and secondary recording phases.
     */
    void recordCommands(std::uint32_t imageIndex, const std::vector<DrawItem>& drawItems,
                        RendererCpuTimings* timings) const;

    /**
     * @brief Records inherited draws and executes them from one primary geometry pass.
     * @param imageIndex Acquired swapchain-image index.
     * @param drawItems Current ordered scene draws.
     * @param timings Optional output receiving both command-buffer recording phases.
     */
    void recordSecondaryCommands(std::uint32_t imageIndex, const std::vector<DrawItem>& drawItems,
                                 RendererCpuTimings* timings) const;

    /**
     * @brief Records the complete geometry pass directly into one primary command buffer.
     * @param imageIndex Acquired swapchain-image index.
     * @param drawItems Current ordered scene draws.
     * @param timings Optional output receiving the direct primary recording phase.
     */
    void recordDirectCommands(std::uint32_t imageIndex, const std::vector<DrawItem>& drawItems,
                              RendererCpuTimings* timings) const;

    /**
     * @brief Begins a primary command buffer and its geometry rendering instance.
     * @param commandBuffer Primary command buffer receiving the frame prefix.
     * @param imageIndex Acquired swapchain-image index used as the color attachment.
     * @param flags Rendering flags selecting direct or secondary-command contents.
     */
    void beginPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer,
                               std::uint32_t imageIndex, vk::RenderingFlags flags) const;

    /**
     * @brief Ends the geometry instance and records the primary command-buffer suffix.
     * @param commandBuffer Primary command buffer receiving the frame suffix.
     * @param imageIndex Acquired swapchain-image index transitioned for presentation.
     */
    void endPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer,
                             std::uint32_t imageIndex) const;

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
     */
    void transitionDepthToAttachment(const vk::raii::CommandBuffer& commandBuffer) const;

    /**
     * @brief Begins dynamic rendering for direct or secondary-command contents.
     * @param commandBuffer Primary command buffer receiving the rendering boundary.
     * @param imageIndex Acquired swapchain-image index used as the color attachment.
     * @param flags Rendering flags selecting direct or secondary-command contents.
     */
    void beginGeometryPass(const vk::raii::CommandBuffer& commandBuffer, std::uint32_t imageIndex,
                           vk::RenderingFlags flags) const;

    /**
     * @brief Records fixed state shared by every draw in one command buffer.
     * @param commandBuffer Primary or secondary command buffer receiving the state.
     * @return Fresh draw-binding cache scoped to the established descriptor state.
     */
    [[nodiscard]] detail::DrawBindingState
    bindGeometryState(const vk::raii::CommandBuffer& commandBuffer) const;

    /**
     * @brief Records the mesh bindings, constants, and indexed draw for each item.
     * @param commandBuffer Command buffer inside the active color pass.
     * @param drawItems Current ordered scene draws.
     * @param bindingState Cache created when the complete geometry state was established.
     */
    void recordDraws(const vk::raii::CommandBuffer& commandBuffer,
                     const std::vector<DrawItem>& drawItems,
                     detail::DrawBindingState bindingState) const;

    /**
     * @brief Orders color writes before the transition to presentation.
     * @param commandBuffer Command buffer receiving the image barrier.
     * @param imageIndex Acquired swapchain-image index.
     */
    void transitionToPresent(const vk::raii::CommandBuffer& commandBuffer,
                             std::uint32_t imageIndex) const;

    // Reverse destruction keeps every allocation ahead of its VMA and Vulkan
    // owners. Presentation lifetime retains the separate Swapchain precondition.

    // Foundational long-lived state.
    CommandRecordingMode commandRecordingMode_; ///< Fixed production or attribution path.
    detail::Device device_;                     ///< Vulkan instance, surface, device, and queues.
    detail::MemoryAllocator allocator_;         ///< VMA owner created from the logical device.
    detail::ResourceCompiler resourceCompiler_; ///< Dedicated setup-time upload context.

    // Presentation-dependent state replaced as one ownership group.
    std::unique_ptr<PresentationState> presentation_; ///< Swapchain-compatible resources.

    detail::FrameSlot frameSlot_;                   ///< Per-frame submission state.
    detail::RecordingContext coordinatorRecording_; ///< Primary command recording state.
    detail::RecordingContext workerRecording_;      ///< Worker-local command recording state.

    // Prepared state compiled from the current scene dependencies.
    RenderPreparation renderPreparation_;           ///< Vulkan-free validation and plan cache.
    detail::CompiledResources compiledResources_;   ///< GPU state selected by the current plan.
    std::optional<std::size_t> compiledGeneration_; ///< Plan generation uploaded to the GPU.
};
/** @endcond */

/* --- Public member functions --- */

Renderer::Renderer(const Glfw& glfw, const Window& window, const std::string& applicationName,
                   RendererConfiguration configuration)
    : implementation_{std::make_unique<Impl>(glfw, window, applicationName, configuration)}
{
}

Renderer::~Renderer() noexcept = default;

void Renderer::prepare(const RenderAssets& assets, const Scene& scene)
{
    implementation_->prepare(assets, scene);
}

RenderResult Renderer::drawFrame(const Scene& scene, RendererCpuTimings* timings)
{
    return implementation_->drawFrame(scene, timings);
}

bool Renderer::recreatePresentation(const Window& window)
{
    return implementation_->recreatePresentation(window);
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
      device_{glfw, window, applicationName},
      allocator_{device_},
      resourceCompiler_{device_, allocator_},
      presentation_{std::make_unique<PresentationState>(device_, allocator_, window)},
      // Frame storage depends on the presentation extent sampled here, so the
      // presentation owner must be constructed before the frame slot.
      frameSlot_{device_, allocator_,
                 detail::FrameUniforms{
                     .viewProjection = createViewProjection(presentation_->swapchain().extent())}},
      coordinatorRecording_{device_, detail::RecordingBufferKind::ePrimary},
      workerRecording_{device_,
                       commandRecordingMode_ == CommandRecordingMode::eSecondaryCommandBuffer
                           ? detail::RecordingBufferKind::eSecondary
                           : detail::RecordingBufferKind::eNone}
{
    if (!*device_.graphicsQueue() || !*device_.presentQueue())
    {
        throw std::runtime_error("Vulkan returned a null device queue");
    }
    if (allocator_.handle() == nullptr)
    {
        throw std::runtime_error("VMA returned a null allocator");
    }
    if (frameSlot_.frameFinished().getStatus() != vk::Result::eSuccess)
    {
        throw std::runtime_error("The frame-finished fence was not initially signaled");
    }
}

Renderer::Impl::~Impl() noexcept
{
    if (!frameSlot_.workMayBePending())
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

void Renderer::Impl::prepare(const RenderAssets& assets, const Scene& scene)
{
    // Planning validates every CPU relationship before the first Vulkan
    // allocation, keeping malformed input failures deterministic and cheap.
    const SceneDrawList drawList = scene.buildDrawItems();
    const RenderPreparationPlan& plan =
        renderPreparation_.build(assets, drawList, presentation_->pipeline().description());
    if (compiledGeneration_.has_value() && *compiledGeneration_ == renderPreparation_.generation())
    {
        return;
    }

    // Replacing compiled buffers requires earlier submissions to have
    // finished using them. Identical preparation inputs return above and
    // avoid both this wait and every allocation below.
    if (frameSlot_.workMayBePending())
    {
        waitIdle();
    }

    std::unique_ptr<detail::CompiledResourceGraph> candidate =
        resourceCompiler_.compile(assets, plan, compiledResources_.graph());
    compiledResources_.replace(std::move(candidate));
    compiledGeneration_ = renderPreparation_.generation();
}

RenderResult Renderer::Impl::drawFrame(const Scene& scene, RendererCpuTimings* timings)
{
    if (timings != nullptr)
    {
        *timings = {};
    }
    if (!compiledGeneration_.has_value())
    {
        throw std::logic_error("Renderer::prepare must be called before drawFrame");
    }
    // Build and validate the transient draw list before acquiring an image.
    // A failure therefore cannot abandon a signaled acquisition semaphore.
    const SceneDrawList drawList = [&]()
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->drawListBuild};
        return scene.buildDrawItems();
    }();
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->drawListValidation};
        for (const DrawItem& item : drawList.drawItems)
        {
            if (!compiledResources_.contains(item.renderObject))
            {
                throw std::logic_error("Scene refers to an object not compiled by prepare");
            }
        }
    }

    const vk::raii::Device& logicalDevice = device_.logicalDevice();
    vk::Result fenceResult = vk::Result::eSuccess;
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->frameFenceWait};
        fenceResult = logicalDevice.waitForFences(*frameSlot_.frameFinished(), vk::True,
                                                  std::numeric_limits<std::uint64_t>::max());
    }
    if (fenceResult != vk::Result::eSuccess)
    {
        throw vk::SystemError{vk::make_error_code(fenceResult), "Waiting for the frame fence"};
    }

    std::uint32_t imageIndex = 0;
    bool swapchainIsSuboptimal = false;
    try
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->imageAcquisitionWait};
        const auto [result, acquiredImageIndex] =
            presentation_->swapchain().handle().acquireNextImage(
                std::numeric_limits<std::uint64_t>::max(), *frameSlot_.imageAvailable());
        imageIndex = acquiredImageIndex;
        swapchainIsSuboptimal = result == vk::Result::eSuboptimalKHR;
    }
    catch (const vk::OutOfDateKHRError&)
    {
        // The fence is still signaled because acquisition happens before
        // its reset, so eventual recreation can reuse this frame slot.
        return RenderResult::eNotPresented;
    }

    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->presentationFenceWait};
        presentation_->preparePresentFence(imageIndex);
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->coordinatorCommandPoolReset};
        coordinatorRecording_.resetCommands();
    }
    recordCommands(imageIndex, drawList.drawItems, timings);
    if (timings != nullptr)
    {
        timings->commandPoolReset =
            timings->coordinatorCommandPoolReset + timings->workerCommandPoolReset;
    }

    // Nothing intentionally abandons the frame after this reset: a
    // successful submission will signal the fence, while errors unwind.
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->queueSubmission};
        logicalDevice.resetFences(*frameSlot_.frameFinished());

        const vk::SemaphoreSubmitInfo waitInfo{
            .semaphore = *frameSlot_.imageAvailable(),
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        };
        const vk::CommandBufferSubmitInfo commandInfo{
            .commandBuffer = *coordinatorRecording_.commandBuffer(),
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
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signalInfo,
        };
        device_.graphicsQueue().submit2(submitInfo, *frameSlot_.frameFinished());
    }
    frameSlot_.markWorkPending();

    const vk::Semaphore renderFinished = *presentation_->swapchain().renderFinished(imageIndex);
    const vk::SwapchainKHR swapchain = *presentation_->swapchain().handle();
    const vk::Fence presentFence = *presentation_->presentFence(imageIndex);
    const vk::SwapchainPresentFenceInfoKHR presentFenceInfo{
        .swapchainCount = 1,
        .pFences = &presentFence,
    };
    const vk::PresentInfoKHR presentInfo{
        .pNext = &presentFenceInfo,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinished,
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
        return RenderResult::eNotPresented;
    }

    return swapchainIsSuboptimal ? RenderResult::ePresentedSuboptimal : RenderResult::ePresented;
}

void Renderer::Impl::waitIdle()
{
    device_.logicalDevice().waitIdle();
    presentation_->waitForPresentations();
    frameSlot_.clearPendingWork();
}

bool Renderer::Impl::recreatePresentation(const Window& window)
{
    const vk::Extent2D framebufferExtent = window.framebufferExtent();
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        return false;
    }

    // Device idle covers command-buffer use of the old color and depth images.
    // Presentation fences separately prove that the presentation engine has
    // released the old swapchain and its binary wait semaphores.
    waitIdle();
    const vk::SwapchainKHR oldSwapchain = *presentation_->swapchain().handle();
    auto replacement =
        std::make_unique<PresentationState>(device_, allocator_, window, oldSwapchain);
    frameSlot_.writeUniforms(detail::FrameUniforms{
        .viewProjection = createViewProjection(replacement->swapchain().extent()),
    });
    presentation_ = std::move(replacement);
    return true;
}

RendererInfo Renderer::Impl::info() const
{
    return {
        .deviceName = device_.name(),
        .driverName = device_.driverName(),
        .driverInfo = device_.driverInfo(),
        .graphicsQueueFamily = device_.graphicsQueueFamily(),
        .presentQueueFamily = device_.presentQueueFamily(),
        .swapchainImageCount = presentation_->swapchain().imageCount(),
        .presentationSemaphoreCount = presentation_->swapchain().renderFinished().size(),
        .width = presentation_->swapchain().extent().width,
        .height = presentation_->swapchain().extent().height,
        .imageFormat = vk::to_string(presentation_->swapchain().imageFormat()),
        .depthFormat = vk::to_string(presentation_->depthBuffer().format()),
        .presentMode = vk::to_string(presentation_->swapchain().presentMode()),
        .commandRecordingMode = commandRecordingMode_,
    };
}

void Renderer::Impl::recordCommands(std::uint32_t imageIndex,
                                    const std::vector<DrawItem>& drawItems,
                                    RendererCpuTimings* timings) const
{
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->workerCommandPoolReset};
        workerRecording_.resetCommands();
    }
    if (commandRecordingMode_ == CommandRecordingMode::eDirectPrimary)
    {
        recordDirectCommands(imageIndex, drawItems, timings);
        return;
    }
    recordSecondaryCommands(imageIndex, drawItems, timings);
}

void Renderer::Impl::recordSecondaryCommands(std::uint32_t imageIndex,
                                             const std::vector<DrawItem>& drawItems,
                                             RendererCpuTimings* timings) const
{
    const vk::raii::CommandBuffer& secondaryCommandBuffer = workerRecording_.commandBuffer();
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->secondaryCommandRecording};
        const vk::Format colorFormat = presentation_->swapchain().imageFormat();
        const vk::CommandBufferInheritanceRenderingInfo renderingInheritance{
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat,
            .depthAttachmentFormat = presentation_->depthBuffer().format(),
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
        secondaryCommandBuffer.begin(secondaryBeginInfo);
        detail::DrawBindingState bindingState = bindGeometryState(secondaryCommandBuffer);
        recordDraws(secondaryCommandBuffer, drawItems, std::move(bindingState));
        secondaryCommandBuffer.end();
    }

    const vk::raii::CommandBuffer& primaryCommandBuffer = coordinatorRecording_.commandBuffer();
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
        beginPrimaryRecording(primaryCommandBuffer, imageIndex,
                              vk::RenderingFlagBits::eContentsSecondaryCommandBuffers);
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->secondaryCommandExecution};
        const std::array secondaryCommands{*secondaryCommandBuffer};
        primaryCommandBuffer.executeCommands(secondaryCommands);
    }
    {
        CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
        endPrimaryRecording(primaryCommandBuffer, imageIndex);
    }
}

void Renderer::Impl::recordDirectCommands(std::uint32_t imageIndex,
                                          const std::vector<DrawItem>& drawItems,
                                          RendererCpuTimings* timings) const
{
    CpuPhaseTimer timer{timings == nullptr ? nullptr : &timings->primaryCommandRecording};
    const vk::raii::CommandBuffer& primaryCommandBuffer = coordinatorRecording_.commandBuffer();
    beginPrimaryRecording(primaryCommandBuffer, imageIndex, {});
    detail::DrawBindingState bindingState = bindGeometryState(primaryCommandBuffer);
    recordDraws(primaryCommandBuffer, drawItems, std::move(bindingState));
    endPrimaryRecording(primaryCommandBuffer, imageIndex);
}

void Renderer::Impl::beginPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer,
                                           std::uint32_t imageIndex, vk::RenderingFlags flags) const
{
    const vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBuffer.begin(beginInfo);
    transitionToAttachment(commandBuffer, imageIndex);
    transitionDepthToAttachment(commandBuffer);
    beginGeometryPass(commandBuffer, imageIndex, flags);
}

void Renderer::Impl::endPrimaryRecording(const vk::raii::CommandBuffer& commandBuffer,
                                         std::uint32_t imageIndex) const
{
    commandBuffer.endRendering();
    transitionToPresent(commandBuffer, imageIndex);
    commandBuffer.end();
}

void Renderer::Impl::transitionDepthToAttachment(const vk::raii::CommandBuffer& commandBuffer) const
{
    // The depth value is cleared before every use, so no previous contents need
    // preserving. The sole frame fence has completed before this command buffer
    // is recorded, making it safe to discard the previous frame's depth writes.
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
        .image = presentation_->depthBuffer().image(),
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
                                       std::uint32_t imageIndex, vk::RenderingFlags flags) const
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
        .imageView = *presentation_->depthBuffer().view(),
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

detail::DrawBindingState
Renderer::Impl::bindGeometryState(const vk::raii::CommandBuffer& commandBuffer) const
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                               *presentation_->pipeline().pipeline());

    const vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(presentation_->swapchain().extent().width),
        .height = static_cast<float>(presentation_->swapchain().extent().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const vk::Rect2D scissor{
        .offset = {.x = 0, .y = 0},
        .extent = presentation_->swapchain().extent(),
    };
    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, scissor);

    const vk::DescriptorBufferInfo uniformInfo{
        .buffer = frameSlot_.uniformBuffer().handle(),
        .offset = 0,
        .range = sizeof(detail::FrameUniforms),
    };
    const vk::WriteDescriptorSet uniformWrite{
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &uniformInfo,
    };
    commandBuffer.pushDescriptorSet(vk::PipelineBindPoint::eGraphics,
                                    *presentation_->pipeline().pipelineLayout(), 0, uniformWrite);
    return {};
}

void Renderer::Impl::recordDraws(const vk::raii::CommandBuffer& commandBuffer,
                                 const std::vector<DrawItem>& drawItems,
                                 detail::DrawBindingState bindingState) const
{
    constexpr vk::DeviceSize bufferOffset = 0;
    for (const DrawItem& item : drawItems)
    {
        const detail::CompiledDraw draw = compiledResources_.draw(item.renderObject);
        assert(draw.vertexLayout == presentation_->pipeline().description().vertexLayout);
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
            commandBuffer.pushDescriptorSet(vk::PipelineBindPoint::eGraphics,
                                            *presentation_->pipeline().pipelineLayout(), 0,
                                            textureWrite);
        }

        const detail::DrawConstants constants{
            .model = item.world,
            .baseColor = draw.baseColor,
        };
        commandBuffer.pushConstants<detail::DrawConstants>(
            *presentation_->pipeline().pipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0,
            constants);
        commandBuffer.drawIndexed(draw.indexCount, 1, 0, 0, 0);
    }
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

namespace
{
/* --- File-local class member functions --- */

PresentationState::PresentationState(const detail::Device& device,
                                     const detail::MemoryAllocator& allocator, const Window& window,
                                     vk::SwapchainKHR oldSwapchain)
    : logicalDevice_{&device.logicalDevice()},
      swapchain_{device, window, oldSwapchain},
      depthBuffer_{device, allocator, swapchain_.extent()},
      pipeline_{device, kScenePipelineDescription, swapchain_.imageFormat(), depthBuffer_.format()},
      presentSubmitted_(swapchain_.imageCount(), 0)
{
    if (swapchain_.imageCount() == 0 ||
        swapchain_.imageViews().size() != swapchain_.images().size() ||
        swapchain_.renderFinished().size() != swapchain_.imageCount())
    {
        throw std::runtime_error("Vulkan returned an incomplete swapchain");
    }

    constexpr vk::FenceCreateInfo fenceInfo{};
    presentFences_.reserve(swapchain_.imageCount());
    for (std::size_t imageIndex = 0; imageIndex < swapchain_.imageCount(); ++imageIndex)
    {
        presentFences_.emplace_back(device.logicalDevice(), fenceInfo);
    }
}

const detail::Swapchain& PresentationState::swapchain() const noexcept
{
    return swapchain_;
}

const detail::DepthBuffer& PresentationState::depthBuffer() const noexcept
{
    return depthBuffer_;
}

const detail::Pipeline& PresentationState::pipeline() const noexcept
{
    return pipeline_;
}

void PresentationState::preparePresentFence(std::size_t imageIndex)
{
    if (presentSubmitted_.at(imageIndex) == 0)
    {
        return;
    }

    const vk::Result result = logicalDevice_->waitForFences(
        *presentFences_.at(imageIndex), vk::True, std::numeric_limits<std::uint64_t>::max());
    if (result != vk::Result::eSuccess)
    {
        throw vk::SystemError{vk::make_error_code(result), "Waiting for presentation completion"};
    }
    logicalDevice_->resetFences(*presentFences_[imageIndex]);
    presentSubmitted_[imageIndex] = 0;
}

const vk::raii::Fence& PresentationState::presentFence(std::size_t imageIndex) const
{
    return presentFences_.at(imageIndex);
}

void PresentationState::markPresentSubmitted(std::size_t imageIndex)
{
    presentSubmitted_.at(imageIndex) = 1;
}

void PresentationState::waitForPresentations()
{
    for (std::size_t imageIndex = 0; imageIndex < presentFences_.size(); ++imageIndex)
    {
        preparePresentFence(imageIndex);
    }
}

/* --- File-local functions --- */

/**
 * @brief Builds the fixed tutorial camera for the current presentation extent.
 * @param extent Non-zero swapchain extent used to derive the projection aspect ratio.
 * @return World-to-clip transform for the static camera.
 * @throws std::logic_error if the fixed camera unexpectedly has a degenerate basis.
 */
[[nodiscard]] Mat4 createViewProjection(vk::Extent2D extent)
{
    const std::expected<Mat4, NormalizeError> view = Mat4::lookAt(
        Vec3{.x = 0.0f, .y = 0.0f, .z = 4.0f}, Vec3{}, Vec3{.x = 0.0f, .y = 1.0f, .z = 0.0f});
    if (!view.has_value())
    {
        throw std::logic_error("The fixed camera produced a degenerate view basis");
    }

    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const Mat4 projection =
        Mat4::perspective(std::numbers::pi_v<float> / 3.0f, aspectRatio, 0.1f, 100.0f);
    return projection * *view;
}

} // namespace
/** @endcond */

} // namespace fire_engine
