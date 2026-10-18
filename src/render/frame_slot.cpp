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
}

const vk::raii::Semaphore& FrameSlot::imageAvailable() const noexcept
{
    return imageAvailable_;
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
