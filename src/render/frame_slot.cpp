#include <fire_engine/render/detail/frame_slot.hpp>

#include <fire_engine/render/detail/device.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

FrameSlot::FrameSlot(const Device& device)
{
    constexpr vk::SemaphoreCreateInfo semaphoreInfo{};
    imageAvailable_ = vk::raii::Semaphore{device.logicalDevice(), semaphoreInfo};

    const vk::FenceCreateInfo fenceInfo{
        .flags = vk::FenceCreateFlagBits::eSignaled,
    };
    frameFinished_ = vk::raii::Fence{device.logicalDevice(), fenceInfo};
}

const vk::raii::Semaphore& FrameSlot::imageAvailable() const noexcept
{
    return imageAvailable_;
}

const vk::raii::Fence& FrameSlot::frameFinished() const noexcept
{
    return frameFinished_;
}

bool FrameSlot::workMayBePending() const noexcept
{
    return workMayBePending_;
}

void FrameSlot::markWorkPending() noexcept
{
    workMayBePending_ = true;
}

void FrameSlot::clearPendingWork() noexcept
{
    workMayBePending_ = false;
}
/** @endcond */
} // namespace fire_engine::detail
