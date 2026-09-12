#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include <fire_engine/platform/framebuffer_extent.hpp>
#include <fire_engine/render/detail/depth_buffer.hpp>
#include <fire_engine/render/detail/forward_pipeline.hpp>
#include <fire_engine/render/detail/frame_slot_count.hpp>
#include <fire_engine/render/detail/swapchain.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;
class MemoryAllocator;

/* --- Classes --- */

/**
 * @brief Replaceable swapchain, attachments, forward pipeline, and presentation completion state.
 */
class PresentationState final
{
public:
    /**
     * @brief Creates one complete set of mutually compatible presentation resources.
     * @param device Device and queues used for rendering and presentation.
     * @param allocator VMA owner used for the depth attachment.
     * @param framebufferExtent Drawable size used to select the swapchain extent.
     * @param captureEnabled Whether presentable images must support readback.
     * @param oldSwapchain Previous swapchain offered for implementation reuse.
     */
    PresentationState(const Device& device, const MemoryAllocator& allocator,
                      FramebufferExtent framebufferExtent, bool captureEnabled,
                      vk::SwapchainKHR oldSwapchain = nullptr);

    /** @brief Returns the owned swapchain. @return Presentation images and semaphores. */
    [[nodiscard]] const Swapchain& swapchain() const noexcept;
    /**
     * @brief Returns the depth attachment belonging to one submission slot.
     * @param frameSlotIndex Cycled submission-slot index, independent of the acquired image.
     * @return Extent-matched depth state safe for that slot's submitted work.
     * @throws std::out_of_range if frameSlotIndex does not identify a submission slot.
     */
    [[nodiscard]] const DepthBuffer& depthBuffer(std::size_t frameSlotIndex) const;
    /** @brief Returns the attachment-compatible forward pipeline. @return Owned pipeline. */
    [[nodiscard]] const ForwardPipeline& forwardPipeline() const noexcept;

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
    // Reverse destruction releases fences and the forward pipeline before attachment
    // resources, and releases the depth allocation before the swapchain.
    const vk::raii::Device* logicalDevice_ = nullptr; ///< Borrowed owner used for fence waits.
    Swapchain swapchain_; ///< Images, views, and per-image binary semaphores.
    // Every entry selects the same device depth format. Forward-pipeline
    // creation and public reporting may therefore use the first entry's format.
    std::array<DepthBuffer, kFrameSlotCount>
        depthBuffers_;                ///< Presentation-dependent attachment per submission slot.
    ForwardPipeline forwardPipeline_; ///< Compatible color/depth forward pipeline.
    std::vector<vk::raii::Fence> presentFences_; ///< Completion fence per image.
    std::vector<std::uint8_t> presentSubmitted_; ///< Whether each fence has pending work.
};
/** @endcond */
} // namespace fire_engine::detail
