#include <fire_engine/render/detail/recording_context.hpp>

#include <fire_engine/render/detail/device.hpp>

#include <cassert>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

RecordingContext::RecordingContext(const Device& device, RecordingBufferKind bufferKind)
{
    const vk::CommandPoolCreateInfo commandPoolInfo{
        .queueFamilyIndex = device.graphicsQueueFamily(),
    };
    commandPool_ = vk::raii::CommandPool{device.logicalDevice(), commandPoolInfo};

    if (bufferKind == RecordingBufferKind::eNone)
    {
        return;
    }
    const vk::CommandBufferAllocateInfo commandBufferInfo{
        .commandPool = *commandPool_,
        .level = bufferKind == RecordingBufferKind::ePrimary ? vk::CommandBufferLevel::ePrimary
                                                             : vk::CommandBufferLevel::eSecondary,
        .commandBufferCount = 1,
    };
    commandBuffers_ = vk::raii::CommandBuffers{device.logicalDevice(), commandBufferInfo};
}

void RecordingContext::resetCommands() const
{
    commandPool_.reset();
}

const vk::raii::CommandBuffer& RecordingContext::commandBuffer() const noexcept
{
    assert(hasCommandBuffer());
    return commandBuffers_.front();
}

bool RecordingContext::hasCommandBuffer() const noexcept
{
    return !commandBuffers_.empty();
}
/** @endcond */
} // namespace fire_engine::detail
