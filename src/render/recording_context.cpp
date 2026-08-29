#include <fire_engine/render/detail/recording_context.hpp>

#include <fire_engine/render/detail/device.hpp>

#include <cassert>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

RecordingContext::RecordingContext(const Device& device, RecordingBufferKind bufferKind)
{
    // Every buffer allocated here is eOneTimeSubmit and the pool is reset
    // before each reuse, so eTransient describes the real usage. It is a driver
    // hint and neither changes nor relaxes the reset rules. Step 8a measured it
    // as unresolved against control drift on both decision-bearing drivers.
    const vk::CommandPoolCreateInfo commandPoolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
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
