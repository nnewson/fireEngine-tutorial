#include <fire_engine/render/detail/presentation_state.hpp>

#include <fire_engine/graphics/pipeline_description.hpp>
#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/capture_format_mapping.hpp>
#include <fire_engine/render/detail/device.hpp>

#include <limits>
#include <stdexcept>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

/** @brief Vulkan-free mesh layout required by the tutorial scene pipeline. */
constexpr PipelineDescription kScenePipelineDescription{};
/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Internal member functions --- */

PresentationState::PresentationState(const Device& device, const MemoryAllocator& allocator,
                                     FramebufferExtent framebufferExtent, bool captureEnabled,
                                     vk::SwapchainKHR oldSwapchain)
    : logicalDevice_{&device.logicalDevice()},
      swapchain_{device, framebufferExtent, captureEnabled, oldSwapchain},
      depthBuffers_{DepthBuffer{device, allocator, swapchain_.extent()},
                    DepthBuffer{device, allocator, swapchain_.extent()}},
      pipeline_{device, kScenePipelineDescription, swapchain_.imageFormat(),
                depthBuffers_.front().format()},
      presentSubmitted_(swapchain_.imageCount(), 0)
{
    if (captureEnabled && !captureFormatFor(swapchain_.imageFormat()).has_value())
    {
        throw std::runtime_error("Frame capture requires an RGBA8 or BGRA8 sRGB swapchain format");
    }
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

const Swapchain& PresentationState::swapchain() const noexcept
{
    return swapchain_;
}

const DepthBuffer& PresentationState::depthBuffer(std::size_t frameSlotIndex) const
{
    return depthBuffers_.at(frameSlotIndex);
}

const Pipeline& PresentationState::pipeline() const noexcept
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
/** @endcond */
} // namespace fire_engine::detail
