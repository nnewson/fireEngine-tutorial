#include <fire_engine/render/detail/frame_barriers.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

// These are characterization tests. Each expected value is copied from the
// barrier its call site recorded before extraction, written as an independent
// literal rather than read back from the production constants used to build
// it. They prove the barriers are unchanged, not that they are correct:
// correctness rests on the synchronization-validation runs and live fault
// controls that exercise these barriers on a device.

namespace
{
namespace barriers = fire_engine::detail;

template <typename Handle, typename NativeHandle>
[[nodiscard]] Handle fakeHandle(std::uintptr_t value)
{
    if constexpr (std::is_pointer_v<NativeHandle>)
    {
        return Handle{reinterpret_cast<NativeHandle>(value)}; // NOLINT(performance-no-int-to-ptr)
    }
    else
    {
        return Handle{static_cast<NativeHandle>(value)};
    }
}

[[nodiscard]] vk::Image fakeImage()
{
    return fakeHandle<vk::Image, VkImage>(0x5000);
}

/**
 * @brief Pins the fields every frame barrier shares: an unextended structure, no
 * queue-family ownership transfer, and the caller's image passed through.
 */
void requireSharedFields(const vk::ImageMemoryBarrier2& barrier, vk::Image image)
{
    REQUIRE(barrier.sType == vk::StructureType::eImageMemoryBarrier2);
    REQUIRE(barrier.pNext == nullptr);
    REQUIRE(barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    REQUIRE(barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    REQUIRE(barrier.image == image);
}

/** @brief Pins a complete range covering the sole mip level and array layer. */
void requireSingleSubresource(const vk::ImageSubresourceRange& range,
                              vk::ImageAspectFlags aspectMask)
{
    REQUIRE(range.aspectMask == aspectMask);
    REQUIRE(range.baseMipLevel == 0);
    REQUIRE(range.levelCount == 1);
    REQUIRE(range.baseArrayLayer == 0);
    REQUIRE(range.layerCount == 1);
}
} // namespace

TEST_CASE("Shadow-map entry barrier discards retired contents for depth writes")
{
    const vk::Image image = fakeImage();
    const vk::ImageMemoryBarrier2 barrier = barriers::shadowMapEntryBarrier(image);

    requireSharedFields(barrier, image);
    // Empty because slot retirement already completed every prior user.
    REQUIRE(barrier.srcStageMask == vk::PipelineStageFlagBits2::eNone);
    REQUIRE(barrier.srcAccessMask == vk::AccessFlagBits2::eNone);
    REQUIRE(barrier.dstStageMask == (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                     vk::PipelineStageFlagBits2::eLateFragmentTests));
    REQUIRE(barrier.dstAccessMask == (vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                      vk::AccessFlagBits2::eDepthStencilAttachmentWrite));
    REQUIRE(barrier.oldLayout == vk::ImageLayout::eUndefined);
    REQUIRE(barrier.newLayout == vk::ImageLayout::eDepthAttachmentOptimal);
    requireSingleSubresource(barrier.subresourceRange, vk::ImageAspectFlagBits::eDepth);
}

TEST_CASE("Shadow-map publication barrier orders depth writes before fragment sampling")
{
    const vk::Image image = fakeImage();
    const vk::ImageMemoryBarrier2 barrier = barriers::shadowMapPublicationBarrier(image);

    requireSharedFields(barrier, image);
    REQUIRE(barrier.srcStageMask == (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                     vk::PipelineStageFlagBits2::eLateFragmentTests));
    REQUIRE(barrier.srcAccessMask == vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
    REQUIRE(barrier.dstStageMask == vk::PipelineStageFlagBits2::eFragmentShader);
    REQUIRE(barrier.dstAccessMask == vk::AccessFlagBits2::eShaderSampledRead);
    REQUIRE(barrier.oldLayout == vk::ImageLayout::eDepthAttachmentOptimal);
    REQUIRE(barrier.newLayout == vk::ImageLayout::eDepthReadOnlyOptimal);
    requireSingleSubresource(barrier.subresourceRange, vk::ImageAspectFlagBits::eDepth);
}

TEST_CASE("Forward color entry barrier discards presentation contents for color writes")
{
    const vk::Image image = fakeImage();
    const vk::ImageMemoryBarrier2 barrier = barriers::forwardColorEntryBarrier(image);

    requireSharedFields(barrier, image);
    REQUIRE(barrier.srcStageMask == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    REQUIRE(barrier.srcAccessMask == vk::AccessFlagBits2::eNone);
    REQUIRE(barrier.dstStageMask == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    REQUIRE(barrier.dstAccessMask == vk::AccessFlagBits2::eColorAttachmentWrite);
    REQUIRE(barrier.oldLayout == vk::ImageLayout::eUndefined);
    REQUIRE(barrier.newLayout == vk::ImageLayout::eAttachmentOptimal);
    requireSingleSubresource(barrier.subresourceRange, vk::ImageAspectFlagBits::eColor);
}

TEST_CASE("Forward depth entry barrier discards retired contents for depth writes")
{
    const vk::Image image = fakeImage();
    const vk::ImageMemoryBarrier2 barrier = barriers::forwardDepthEntryBarrier(image);

    requireSharedFields(barrier, image);
    // Empty because slot retirement already completed every prior user.
    REQUIRE(barrier.srcStageMask == vk::PipelineStageFlagBits2::eNone);
    REQUIRE(barrier.srcAccessMask == vk::AccessFlagBits2::eNone);
    REQUIRE(barrier.dstStageMask == (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                     vk::PipelineStageFlagBits2::eLateFragmentTests));
    REQUIRE(barrier.dstAccessMask == (vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                      vk::AccessFlagBits2::eDepthStencilAttachmentWrite));
    REQUIRE(barrier.oldLayout == vk::ImageLayout::eUndefined);
    REQUIRE(barrier.newLayout == vk::ImageLayout::eDepthAttachmentOptimal);
    requireSingleSubresource(barrier.subresourceRange, vk::ImageAspectFlagBits::eDepth);
}

TEST_CASE("Presentation barrier makes color writes available for presentation")
{
    const vk::Image image = fakeImage();
    const vk::ImageMemoryBarrier2 barrier = barriers::presentationBarrier(image);

    requireSharedFields(barrier, image);
    REQUIRE(barrier.srcStageMask == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    REQUIRE(barrier.srcAccessMask == vk::AccessFlagBits2::eColorAttachmentWrite);
    REQUIRE(barrier.dstStageMask == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    REQUIRE(barrier.dstAccessMask == vk::AccessFlagBits2::eNone);
    REQUIRE(barrier.oldLayout == vk::ImageLayout::eAttachmentOptimal);
    REQUIRE(barrier.newLayout == vk::ImageLayout::ePresentSrcKHR);
    requireSingleSubresource(barrier.subresourceRange, vk::ImageAspectFlagBits::eColor);
}

TEST_CASE("Capture copy-source barrier orders color writes before the readback copy")
{
    const vk::Image image = fakeImage();
    const vk::ImageMemoryBarrier2 barrier = barriers::captureCopySourceBarrier(image);

    requireSharedFields(barrier, image);
    REQUIRE(barrier.srcStageMask == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    REQUIRE(barrier.srcAccessMask == vk::AccessFlagBits2::eColorAttachmentWrite);
    REQUIRE(barrier.dstStageMask == vk::PipelineStageFlagBits2::eTransfer);
    REQUIRE(barrier.dstAccessMask == vk::AccessFlagBits2::eTransferRead);
    REQUIRE(barrier.oldLayout == vk::ImageLayout::eAttachmentOptimal);
    // The readback copy declares this as its source layout.
    REQUIRE(barrier.newLayout == vk::ImageLayout::eTransferSrcOptimal);
    requireSingleSubresource(barrier.subresourceRange, vk::ImageAspectFlagBits::eColor);
}

TEST_CASE("Capture presentation barrier leaves its destination empty for host retirement")
{
    const vk::Image image = fakeImage();
    const vk::ImageMemoryBarrier2 barrier = barriers::capturePresentationBarrier(image);

    requireSharedFields(barrier, image);
    REQUIRE(barrier.srcStageMask == vk::PipelineStageFlagBits2::eTransfer);
    REQUIRE(barrier.srcAccessMask == vk::AccessFlagBits2::eTransferRead);
    // Capture waits for the whole submission on the host before presenting and
    // signals no render-finished semaphore, so no device work follows.
    REQUIRE(barrier.dstStageMask == vk::PipelineStageFlagBits2::eNone);
    REQUIRE(barrier.dstAccessMask == vk::AccessFlagBits2::eNone);
    REQUIRE(barrier.oldLayout == vk::ImageLayout::eTransferSrcOptimal);
    REQUIRE(barrier.newLayout == vk::ImageLayout::ePresentSrcKHR);
    requireSingleSubresource(barrier.subresourceRange, vk::ImageAspectFlagBits::eColor);
}

TEST_CASE("Frame semaphore stages are pinned exactly")
{
    // Exact values: a widened stage still satisfies the chain checks below,
    // yet changes the submitted semaphore scopes.
    REQUIRE(barriers::kAcquireWaitStage == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    REQUIRE(barriers::kRenderFinishedSignalStage ==
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);
}

TEST_CASE("Swapchain barriers chain with the frame semaphore stages")
{
    // A shared stage is a conservative sufficient condition for each execution
    // dependency chain. These guard the relation if a value and its exact pin
    // are later changed together.
    const vk::Image image = fakeImage();

    SECTION("Color entry chains from the acquire wait")
    {
        REQUIRE((barriers::forwardColorEntryBarrier(image).srcStageMask &
                 barriers::kAcquireWaitStage) != vk::PipelineStageFlags2{});
    }

    SECTION("Presentation chains into the render-finished signal")
    {
        REQUIRE((barriers::presentationBarrier(image).dstStageMask &
                 barriers::kRenderFinishedSignalStage) != vk::PipelineStageFlags2{});
    }
}
