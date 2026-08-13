#include <fire_engine/render/renderer.hpp>

#include <fire_engine/core/log.hpp>
#include <fire_engine/graphics/render_assets.hpp>
#include <fire_engine/graphics/render_preparation.hpp>
#include <fire_engine/math/normalize_error.hpp>
#include <fire_engine/math/vec3.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/buffer.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/draw_constants.hpp>
#include <fire_engine/render/detail/frame_in_flight.hpp>
#include <fire_engine/render/detail/image.hpp>
#include <fire_engine/render/detail/pipeline.hpp>
#include <fire_engine/render/detail/swapchain.hpp>
#include <fire_engine/scene/scene.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <numbers>
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

/** @brief The sole color mip and array layer used by every image barrier. */
constexpr vk::ImageSubresourceRange kColorSubresourceRange{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};

/** @brief The sole mip and array layer used by the depth attachment. */
constexpr vk::ImageSubresourceRange kDepthSubresourceRange{
    .aspectMask = vk::ImageAspectFlagBits::eDepth,
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
    /**
     * @brief Uploads one validated CPU mesh into vertex and index buffers.
     * @param allocator VMA owner used to create both buffers.
     * @param mesh Validated CPU geometry copied into the buffers.
     */
    CompiledMesh(const detail::MemoryAllocator& allocator, const Mesh& mesh);

    /** @brief Returns the uploaded vertex buffer. @return Vertex-buffer allocation. */
    [[nodiscard]] const detail::AllocatedBuffer& vertexBuffer() const noexcept;
    /** @brief Returns the uploaded index buffer. @return Index-buffer allocation. */
    [[nodiscard]] const detail::AllocatedBuffer& indexBuffer() const noexcept;
    /** @brief Returns the number of indices uploaded for drawing. @return Index count. */
    [[nodiscard]] std::uint32_t indexCount() const noexcept;

private:
    detail::AllocatedBuffer vertexBuffer_; ///< GPU buffer containing tightly packed vertices.
    detail::AllocatedBuffer indexBuffer_;  ///< GPU buffer containing 32-bit triangle indices.
    std::uint32_t indexCount_;             ///< Number of indices consumed by drawIndexed.
};

/** @brief Extent-dependent depth attachment paired with the swapchain. */
class DepthBuffer final
{
public:
    /**
     * @brief Selects a supported format and creates one depth image and view.
     * @param device Physical and logical device used for selection and view creation.
     * @param allocator VMA owner used for the image allocation.
     * @param extent Swapchain extent shared by the depth attachment.
     * @throws std::runtime_error if no supported depth-only format is available.
     */
    DepthBuffer(const detail::Device& device, const detail::MemoryAllocator& allocator,
                vk::Extent2D extent);

    /** @brief Returns the selected depth format. @return Depth attachment format. */
    [[nodiscard]] vk::Format format() const noexcept;
    /** @brief Returns the allocated image. @return Non-owning depth image handle. */
    [[nodiscard]] vk::Image image() const noexcept;
    /** @brief Returns the depth-only image view. @return Owned depth view. */
    [[nodiscard]] const vk::raii::ImageView& view() const noexcept;

private:
    vk::Format format_;            ///< Supported depth-only attachment format.
    detail::AllocatedImage image_; ///< Extent-matched depth image and allocation.
    vk::raii::ImageView view_;     ///< View destroyed before image_.
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
    CompiledImage(const detail::Device& device, const detail::MemoryAllocator& allocator,
                  const ImageData& source);

    /** @brief Returns the allocated Vulkan image. @return Non-owning image handle. */
    [[nodiscard]] vk::Image image() const noexcept;
    /** @brief Returns the shader-visible color view. @return Owned image view. */
    [[nodiscard]] const vk::raii::ImageView& view() const noexcept;

private:
    // The view is declared after the allocation so reverse destruction releases
    // it before VMA destroys the image it references.
    detail::AllocatedImage image_; ///< Device-local image and allocation.
    vk::raii::ImageView view_;     ///< Single-mip color view into image_.
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
    CompiledTexture(const detail::Device& device, const CompiledImage& image,
                    const Texture& source);

    /** @brief Returns the sampled image. @return Borrowed compiled image. */
    [[nodiscard]] const CompiledImage& image() const noexcept;
    /** @brief Returns the Vulkan sampler. @return Owned sampler. */
    [[nodiscard]] const vk::raii::Sampler& sampler() const noexcept;

private:
    const CompiledImage* image_ = nullptr; ///< Borrowed image retained by renderer ordering.
    vk::raii::Sampler sampler_;            ///< Filtering and addressing state.
};

/** @brief One decoded image and staging buffer awaiting a transfer command. */
struct PendingImageUpload
{
    std::unique_ptr<detail::AllocatedBuffer> staging; ///< Host-visible transfer source.
    const ImageData* source = nullptr;                ///< CPU pixels and copy extent.
    const CompiledImage* destination = nullptr;       ///< Device-local transfer destination.
};

/** @brief Prepared lookup target for one RenderObjectId. */
struct CompiledRenderObject
{
    const CompiledMesh* mesh = nullptr;       ///< Shared compiled geometry.
    const CompiledTexture* texture = nullptr; ///< Sampled base-color texture.
    Color4 baseColor{};                       ///< Material factor pushed for each draw.
};

/* --- File-local function declarations --- */

[[nodiscard]] vk::Filter compileFilter(TextureFilter filter);
[[nodiscard]] vk::SamplerAddressMode compileWrap(TextureWrap wrap);
[[nodiscard]] vk::Format chooseDepthFormat(const vk::raii::PhysicalDevice& physicalDevice);
[[nodiscard]] Mat4 createViewProjection(vk::Extent2D extent);
void uploadImages(const detail::Device& device, detail::FrameInFlight& frame,
                  std::span<const PendingImageUpload> uploads);
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
     */
    Impl(const Glfw& glfw, const Window& window, const std::string& applicationName);

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
     * @return Presentation outcome for the acquired swapchain image.
     */
    [[nodiscard]] RenderResult drawFrame(const Scene& scene);

    /** @brief Waits for device work and clears pending-submission bookkeeping. */
    void waitIdle();

    /** @brief Describes the selected Vulkan and presentation state. @return Public summary. */
    [[nodiscard]] RendererInfo info() const;

private:
    /**
     * @brief Records the complete command-buffer sequence for one acquired image.
     * @param imageIndex Acquired swapchain-image index.
     * @param drawItems Current ordered scene draws.
     */
    void recordCommands(std::uint32_t imageIndex, const std::vector<DrawItem>& drawItems) const;

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
     * @brief Begins dynamic rendering and binds state shared by every draw.
     * @param commandBuffer Command buffer receiving rendering and binding commands.
     * @param imageIndex Acquired swapchain-image index used as the color attachment.
     */
    void beginGeometryPass(const vk::raii::CommandBuffer& commandBuffer,
                           std::uint32_t imageIndex) const;

    /**
     * @brief Records the mesh bindings, constants, and indexed draw for each item.
     * @param commandBuffer Command buffer inside the active color pass.
     * @param drawItems Current ordered scene draws.
     */
    void recordDraws(const vk::raii::CommandBuffer& commandBuffer,
                     const std::vector<DrawItem>& drawItems) const;

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
    detail::Device device_;             ///< Vulkan instance, surface, device, and queues.
    detail::MemoryAllocator allocator_; ///< VMA owner created from the logical device.

    // Presentation-dependent state replaced together when recreation is added.
    detail::Swapchain swapchain_; ///< Images and synchronization tied to presentation.
    DepthBuffer depthBuffer_;     ///< Depth attachment matching the swapchain extent.
    detail::Pipeline pipeline_;   ///< Pipeline compatible with both attachment formats.

    // Per-frame submission state.
    // Its initializer reads swapchain_.extent(), so frame_ must remain declared after swapchain_.
    detail::FrameInFlight frame_;   ///< Reusable resources for the current frame slot.
    bool workMayBePending_ = false; ///< Whether destruction requires a defensive wait.

    // Prepared state compiled from the current scene dependencies.
    RenderPreparation renderPreparation_; ///< Vulkan-free validation and plan cache.
    std::vector<std::unique_ptr<CompiledImage>> compiledImages_;     ///< Image lookup by ImageId.
    std::vector<std::unique_ptr<CompiledTexture>> compiledTextures_; ///< Texture lookup by ID.
    std::unique_ptr<CompiledImage> fallbackImage_;     ///< White image for untextured materials.
    std::unique_ptr<CompiledTexture> fallbackTexture_; ///< Sampler paired with fallbackImage_.
    std::vector<std::unique_ptr<CompiledMesh>> compiledMeshes_; ///< Mesh lookup by MeshId.
    std::vector<CompiledRenderObject> compiledObjects_;         ///< Draw lookup by RenderObjectId.
    std::optional<std::size_t> compiledGeneration_; ///< Plan generation uploaded to the GPU.
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

/** @cond INTERNAL */
/* --- Private implementation member functions --- */

Renderer::Impl::Impl(const Glfw& glfw, const Window& window, const std::string& applicationName)
    : device_{glfw, window, applicationName},
      allocator_{device_},
      swapchain_{device_, window},
      depthBuffer_{device_, allocator_, swapchain_.extent()},
      pipeline_{device_, swapchain_.imageFormat(), depthBuffer_.format()},
      frame_{device_, allocator_,
             detail::FrameUniforms{.viewProjection = createViewProjection(swapchain_.extent())}}
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

Renderer::Impl::~Impl() noexcept
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

void Renderer::Impl::prepare(const RenderAssets& assets, const Scene& scene)
{
    // Planning validates every CPU relationship before the first Vulkan
    // allocation, keeping malformed input failures deterministic and cheap.
    const SceneDrawList drawList = scene.buildDrawItems();
    const RenderPreparationPlan& plan = renderPreparation_.build(assets, drawList);
    if (compiledGeneration_.has_value() && *compiledGeneration_ == renderPreparation_.generation())
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

    // Every sampled image is first filled through a host-visible transfer
    // buffer. A one-pixel white texture gives materials without an image the
    // same descriptor path without branching in the shader.
    const ImageData fallbackSource{
        .width = 1,
        .height = 1,
        .pixels = {255, 255, 255, 255},
    };
    std::unique_ptr<CompiledImage> newFallbackImage;
    std::unique_ptr<CompiledTexture> newFallbackTexture;

    std::vector<std::unique_ptr<CompiledImage>> compiledImages(assets.images().size());
    std::vector<PendingImageUpload> uploads;
    uploads.reserve(plan.images.size() + (fallbackImage_ == nullptr ? 1 : 0));

    const auto stageImage = [&](const ImageData& source, const CompiledImage& destination)
    {
        auto staging = std::make_unique<detail::AllocatedBuffer>(
            allocator_, source.pixels.size(), vk::BufferUsageFlagBits::eTransferSrc);
        staging->write(std::as_bytes(std::span{source.pixels}));
        uploads.push_back({
            .staging = std::move(staging),
            .source = &source,
            .destination = &destination,
        });
    };

    if (fallbackImage_ == nullptr)
    {
        newFallbackImage = std::make_unique<CompiledImage>(device_, allocator_, fallbackSource);
        stageImage(fallbackSource, *newFallbackImage);
    }
    for (const ImageId imageId : plan.images)
    {
        const ImageData& source = assets.images()[imageId.value];
        compiledImages[imageId.value] =
            std::make_unique<CompiledImage>(device_, allocator_, source);
        stageImage(source, *compiledImages[imageId.value]);
    }
    if (!uploads.empty())
    {
        uploadImages(device_, frame_, uploads);
    }

    if (newFallbackImage != nullptr)
    {
        const Texture fallbackDescription{};
        newFallbackTexture =
            std::make_unique<CompiledTexture>(device_, *newFallbackImage, fallbackDescription);
    }
    // The local owns the fallback only during its first preparation, before it
    // moves into the persistent member. Later rebuilds select that member.
    const CompiledTexture* const fallback =
        newFallbackTexture != nullptr ? newFallbackTexture.get() : fallbackTexture_.get();
    if (fallback == nullptr)
    {
        throw std::logic_error("Renderer fallback texture was not initialized");
    }
    std::vector<std::unique_ptr<CompiledTexture>> compiledTextures(assets.textures().size());
    for (const TextureId textureId : plan.textures)
    {
        const Texture& source = assets.textures()[textureId.value];
        compiledTextures[textureId.value] =
            std::make_unique<CompiledTexture>(device_, *compiledImages[source.image.value], source);
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
        const Material& material = assets.materials()[object.material.value];
        compiledObjects[object.id.value] = {
            .mesh = compiledMeshes[object.mesh.value].get(),
            .texture = material.baseColorTexture.has_value()
                           ? compiledTextures[material.baseColorTexture->value].get()
                           : fallback,
            .baseColor = material.baseColor,
        };
    }

    // Replace borrowers before their owners, mirroring the reverse-destruction
    // order encoded by the member declarations.
    compiledObjects_ = std::move(compiledObjects);
    compiledMeshes_ = std::move(compiledMeshes);
    if (newFallbackTexture != nullptr)
    {
        fallbackTexture_ = std::move(newFallbackTexture);
        fallbackImage_ = std::move(newFallbackImage);
    }
    compiledTextures_ = std::move(compiledTextures);
    compiledImages_ = std::move(compiledImages);
    compiledGeneration_ = renderPreparation_.generation();
}

RenderResult Renderer::Impl::drawFrame(const Scene& scene)
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
            compiledObjects_[item.renderObject.value].mesh == nullptr ||
            compiledObjects_[item.renderObject.value].texture == nullptr)
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

    return swapchainIsSuboptimal ? RenderResult::ePresentedSuboptimal : RenderResult::ePresented;
}

void Renderer::Impl::waitIdle()
{
    device_.logicalDevice().waitIdle();
    workMayBePending_ = false;
}

RendererInfo Renderer::Impl::info() const
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
        .depthFormat = vk::to_string(depthBuffer_.format()),
        .presentMode = vk::to_string(swapchain_.presentMode()),
    };
}

void Renderer::Impl::recordCommands(std::uint32_t imageIndex,
                                    const std::vector<DrawItem>& drawItems) const
{
    const vk::raii::CommandBuffer& commandBuffer = frame_.commandBuffer();
    const vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    commandBuffer.begin(beginInfo);

    transitionToAttachment(commandBuffer, imageIndex);
    transitionDepthToAttachment(commandBuffer);
    beginGeometryPass(commandBuffer, imageIndex);
    recordDraws(commandBuffer, drawItems);
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
        .image = depthBuffer_.image(),
        .subresourceRange = kDepthSubresourceRange,
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
        .image = swapchain_.image(imageIndex),
        .subresourceRange = kColorSubresourceRange,
    };
    const vk::DependencyInfo beginDependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toAttachment,
    };
    commandBuffer.pipelineBarrier2(beginDependency);
}

void Renderer::Impl::beginGeometryPass(const vk::raii::CommandBuffer& commandBuffer,
                                       std::uint32_t imageIndex) const
{
    const vk::ClearValue clearValue{
        .color = {.float32 = std::array{0.015f, 0.02f, 0.03f, 1.0f}},
    };
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = *swapchain_.imageView(imageIndex),
        .imageLayout = vk::ImageLayout::eAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearValue,
    };
    const vk::ClearValue depthClear{
        .depthStencil = {.depth = 1.0f, .stencil = 0},
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = *depthBuffer_.view(),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = depthClear,
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
        .pDepthAttachment = &depthAttachment,
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
        .range = sizeof(detail::FrameUniforms),
    };
    const vk::WriteDescriptorSet uniformWrite{
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &uniformInfo,
    };
    commandBuffer.pushDescriptorSet(vk::PipelineBindPoint::eGraphics, *pipeline_.pipelineLayout(),
                                    0, uniformWrite);
}

void Renderer::Impl::recordDraws(const vk::raii::CommandBuffer& commandBuffer,
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

        const vk::DescriptorImageInfo textureInfo{
            .sampler = *object.texture->sampler(),
            .imageView = *object.texture->image().view(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        const vk::WriteDescriptorSet textureWrite{
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &textureInfo,
        };
        commandBuffer.pushDescriptorSet(vk::PipelineBindPoint::eGraphics,
                                        *pipeline_.pipelineLayout(), 0, textureWrite);

        const detail::DrawConstants constants{
            .model = item.world,
            .baseColor = object.baseColor,
        };
        commandBuffer.pushConstants<detail::DrawConstants>(
            *pipeline_.pipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, constants);
        commandBuffer.drawIndexed(object.mesh->indexCount(), 1, 0, 0, 0);
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
        .image = swapchain_.image(imageIndex),
        .subresourceRange = kColorSubresourceRange,
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

DepthBuffer::DepthBuffer(const detail::Device& device, const detail::MemoryAllocator& allocator,
                         vk::Extent2D extent)
    : format_{chooseDepthFormat(device.physicalDevice())},
      image_{allocator, extent.width, extent.height, format_,
             vk::ImageUsageFlagBits::eDepthStencilAttachment},
      view_{device.logicalDevice(), vk::ImageViewCreateInfo{
                                        .image = image_.handle(),
                                        .viewType = vk::ImageViewType::e2D,
                                        .format = format_,
                                        .subresourceRange = kDepthSubresourceRange,
                                    }}
{
}

vk::Format DepthBuffer::format() const noexcept
{
    return format_;
}

vk::Image DepthBuffer::image() const noexcept
{
    return image_.handle();
}

const vk::raii::ImageView& DepthBuffer::view() const noexcept
{
    return view_;
}

CompiledImage::CompiledImage(const detail::Device& device, const detail::MemoryAllocator& allocator,
                             const ImageData& source)
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

vk::Image CompiledImage::image() const noexcept
{
    return image_.handle();
}

const vk::raii::ImageView& CompiledImage::view() const noexcept
{
    return view_;
}

CompiledTexture::CompiledTexture(const detail::Device& device, const CompiledImage& image,
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

const CompiledImage& CompiledTexture::image() const noexcept
{
    return *image_;
}

const vk::raii::Sampler& CompiledTexture::sampler() const noexcept
{
    return sampler_;
}

CompiledMesh::CompiledMesh(const detail::MemoryAllocator& allocator, const Mesh& mesh)
    : vertexBuffer_{allocator, mesh.vertices.size() * sizeof(Vertex),
                    vk::BufferUsageFlagBits::eVertexBuffer},
      indexBuffer_{allocator, mesh.indices.size() * sizeof(std::uint32_t),
                   vk::BufferUsageFlagBits::eIndexBuffer},
      indexCount_{static_cast<std::uint32_t>(mesh.indices.size())}
{
    vertexBuffer_.write(std::as_bytes(std::span{mesh.vertices}));
    indexBuffer_.write(std::as_bytes(std::span{mesh.indices}));
}

const detail::AllocatedBuffer& CompiledMesh::vertexBuffer() const noexcept
{
    return vertexBuffer_;
}

const detail::AllocatedBuffer& CompiledMesh::indexBuffer() const noexcept
{
    return indexBuffer_;
}

std::uint32_t CompiledMesh::indexCount() const noexcept
{
    return indexCount_;
}

/* --- File-local functions --- */

/**
 * @brief Selects the first supported depth-only attachment format.
 * @param physicalDevice Device whose optimal-tiling format support is queried.
 * @return Supported format suitable for depth attachment use.
 * @throws std::runtime_error if neither tutorial depth format is supported.
 */
[[nodiscard]] vk::Format chooseDepthFormat(const vk::raii::PhysicalDevice& physicalDevice)
{
    constexpr std::array candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD16Unorm,
    };
    for (const vk::Format candidate : candidates)
    {
        const vk::FormatProperties properties = physicalDevice.getFormatProperties(candidate);
        if ((properties.optimalTilingFeatures &
             vk::FormatFeatureFlagBits::eDepthStencilAttachment) != vk::FormatFeatureFlags{})
        {
            return candidate;
        }
    }
    throw std::runtime_error("The selected device supports no depth-only attachment format");
}

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
void uploadImages(const detail::Device& device, detail::FrameInFlight& frame,
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
        throw vk::SystemError{vk::make_error_code(result), "Waiting for texture uploads"};
    }
}
} // namespace
/** @endcond */

} // namespace fire_engine
