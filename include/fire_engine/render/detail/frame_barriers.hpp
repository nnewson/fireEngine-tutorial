#pragma once

#include <vulkan/vulkan.hpp>

#include <fire_engine/render/detail/image_subresource_ranges.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Constants --- */

/**
 * @brief Stage at which each frame submission waits for its acquired swapchain image.
 *
 * forwardColorEntryBarrier() chains from this stage. Changing either side alone
 * can detach the first color write from image acquisition.
 */
inline constexpr vk::PipelineStageFlags2 kAcquireWaitStage =
    vk::PipelineStageFlagBits2::eColorAttachmentOutput;

/**
 * @brief Stage whose completion signals an ordinary frame's render-finished semaphore.
 *
 * presentationBarrier() chains into this stage. Changing either side alone can
 * let presentation begin before the layout transition completes.
 */
inline constexpr vk::PipelineStageFlags2 kRenderFinishedSignalStage =
    vk::PipelineStageFlagBits2::eColorAttachmentOutput;

/* --- Functions --- */

/**
 * @brief Discards a retired shadow map and establishes depth-attachment access.
 *
 * The slot's timeline wait retired all prior use, including sampled reads. The
 * source is empty because this transition discards those retired contents.
 *
 * @param shadowMap Selected frame-slot shadow-map image.
 * @return Barrier entering the shadow pass's depth-attachment layout.
 */
[[nodiscard]] constexpr vk::ImageMemoryBarrier2 shadowMapEntryBarrier(vk::Image shadowMap) noexcept
{
    return {
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
        .image = shadowMap,
        .subresourceRange = kDepthSubresourceRange,
    };
}

/**
 * @brief Publishes shadow-pass depth writes to later fragment sampled reads.
 *
 * The shadow primary precedes the forward primary in the same submission, so
 * this barrier orders every forward-pass sampled read after the depth writes.
 *
 * @param shadowMap Selected frame-slot shadow map containing the recorded depth writes.
 * @return Barrier entering the read-only layout sampled by the forward pass.
 */
[[nodiscard]] constexpr vk::ImageMemoryBarrier2
shadowMapPublicationBarrier(vk::Image shadowMap) noexcept
{
    return {
        .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
        .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .newLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = shadowMap,
        .subresourceRange = kDepthSubresourceRange,
    };
}

/**
 * @brief Discards previous presentation contents and establishes color-attachment writes.
 *
 * The color image is cleared, so previous presentation contents are discarded.
 * The source stage chains from kAcquireWaitStage, which orders this transition
 * after the presentation engine releases the acquired image.
 *
 * @param swapchainImage Acquired swapchain image rendered by the forward pass.
 * @return Barrier entering the forward pass's color-attachment layout.
 */
[[nodiscard]] constexpr vk::ImageMemoryBarrier2
forwardColorEntryBarrier(vk::Image swapchainImage) noexcept
{
    return {
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImage,
        .subresourceRange = kColorSubresourceRange,
    };
}

/**
 * @brief Discards a retired forward depth image and establishes depth-attachment access.
 *
 * The depth value is cleared before every use. Slot retirement makes it safe to
 * discard that slot's previous writes rather than preserving contents.
 *
 * Its fields currently equal shadowMapEntryBarrier(). They remain separate
 * because they transition different resources with separate retirement contracts.
 *
 * @param depthImage Selected frame-slot forward depth image.
 * @return Barrier entering the forward pass's depth-attachment layout.
 */
[[nodiscard]] constexpr vk::ImageMemoryBarrier2
forwardDepthEntryBarrier(vk::Image depthImage) noexcept
{
    return {
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
        .image = depthImage,
        .subresourceRange = kDepthSubresourceRange,
    };
}

/**
 * @brief Makes an ordinary frame's color writes available for presentation.
 *
 * The destination stage chains into kRenderFinishedSignalStage, so the
 * render-finished semaphore that presentation waits on follows this transition.
 *
 * @param swapchainImage Rendered swapchain image about to be presented.
 * @return Barrier entering the presentation layout.
 */
[[nodiscard]] constexpr vk::ImageMemoryBarrier2
presentationBarrier(vk::Image swapchainImage) noexcept
{
    return {
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eNone,
        .oldLayout = vk::ImageLayout::eAttachmentOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImage,
        .subresourceRange = kColorSubresourceRange,
    };
}

/**
 * @brief Orders a captured frame's color writes before its readback copy.
 *
 * The new layout must match the copy's declared source layout.
 *
 * @param swapchainImage Rendered swapchain image selected for capture.
 * @return Barrier entering the transfer-source layout.
 */
[[nodiscard]] constexpr vk::ImageMemoryBarrier2
captureCopySourceBarrier(vk::Image swapchainImage) noexcept
{
    return {
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eAttachmentOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImage,
        .subresourceRange = kColorSubresourceRange,
    };
}

/**
 * @brief Returns a captured swapchain image to the presentation layout after its copy.
 *
 * The destination is empty. Capture retires the complete submission on the host
 * before presenting and signals no render-finished semaphore, so no later device
 * work in this submission depends on the transition.
 *
 * @param swapchainImage Captured swapchain image about to be presented.
 * @return Barrier entering the presentation layout.
 */
[[nodiscard]] constexpr vk::ImageMemoryBarrier2
capturePresentationBarrier(vk::Image swapchainImage) noexcept
{
    return {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eNone,
        .dstAccessMask = vk::AccessFlagBits2::eNone,
        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImage,
        .subresourceRange = kColorSubresourceRange,
    };
}
/** @endcond */
} // namespace fire_engine::detail
