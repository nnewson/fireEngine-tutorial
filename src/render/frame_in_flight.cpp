#include <fire_engine/render/detail/frame_in_flight.hpp>

#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/device.hpp>

#include <cassert>
#include <span>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

FrameInFlight::FrameInFlight(const Device& device, const MemoryAllocator& allocator,
                             const FrameUniforms& initialUniforms,
                             SecondaryCommandPoolMode secondaryPoolMode,
                             bool allocateSecondaryCommandBuffer)
    : uniformBuffer_{allocator, sizeof(FrameUniforms), vk::BufferUsageFlagBits::eUniformBuffer}
{
    uniformBuffer_.write(std::as_bytes(std::span{&initialUniforms, 1}));

    // No creation flags. eResetCommandBuffer would still allow resetting the
    // whole pool, but it obliges the driver to make every buffer independently
    // resettable, typically its own allocation, rather than bump-allocating the
    // pool as a single arena. Omitting it lets the implementation optimize for
    // whole-pool recycling, which is all this needs: one pool per frame in
    // flight means no buffer ever has to be reset on its own.
    const vk::CommandPoolCreateInfo commandPoolInfo{
        .queueFamilyIndex = device.graphicsQueueFamily(),
    };
    commandPool_ = vk::raii::CommandPool{device.logicalDevice(), commandPoolInfo};

    // The primary buffer is submitted directly and owns the frame boundaries.
    const vk::CommandBufferAllocateInfo commandBufferInfo{
        .commandPool = *commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    commandBuffers_ = vk::raii::CommandBuffers{device.logicalDevice(), commandBufferInfo};

    // Step 2b temporarily selects whether the independently recordable
    // secondary shares the primary pool or uses a worker-shaped pool. Split
    // direct-primary runs still create the second pool, but intentionally leave
    // it without command-buffer allocations so its reset measures empty-pool cost.
    if (secondaryPoolMode == SecondaryCommandPoolMode::eSeparate)
    {
        secondaryCommandPool_ = vk::raii::CommandPool{device.logicalDevice(), commandPoolInfo};
    }
    if (allocateSecondaryCommandBuffer)
    {
        const vk::CommandPool secondaryPool =
            secondaryPoolMode == SecondaryCommandPoolMode::eSeparate ? *secondaryCommandPool_
                                                                     : *commandPool_;
        const vk::CommandBufferAllocateInfo secondaryCommandBufferInfo{
            .commandPool = secondaryPool,
            .level = vk::CommandBufferLevel::eSecondary,
            .commandBufferCount = 1,
        };
        secondaryCommandBuffers_ =
            vk::raii::CommandBuffers{device.logicalDevice(), secondaryCommandBufferInfo};
    }

    // With no VkSemaphoreTypeCreateInfo in its pNext chain, Vulkan creates a
    // binary semaphore. Swapchain acquisition and presentation both require
    // binary semaphores in the render loop.
    constexpr vk::SemaphoreCreateInfo semaphoreInfo{};
    imageAvailable_ = vk::raii::Semaphore{device.logicalDevice(), semaphoreInfo};

    // This fence signals when the submitted work has finished executing.
    // Presentation is separate work that runs afterwards on the present queue,
    // so the fence proves nothing about whether presentation is still waiting
    // on a render-finished semaphore. That is why those semaphores are indexed
    // by swapchain image and owned by Swapchain rather than by this class.
    //
    // The frame loop waits for this fence at the start of every frame, so it
    // begins signaled to let the first frame pass that wait before any work has
    // yet been submitted.
    const vk::FenceCreateInfo fenceInfo{
        .flags = vk::FenceCreateFlagBits::eSignaled,
    };
    frameFinished_ = vk::raii::Fence{device.logicalDevice(), fenceInfo};
}

void FrameInFlight::resetPrimaryCommands() const
{
    commandPool_.reset();
}

void FrameInFlight::resetSecondaryCommands() const
{
    assert(static_cast<VkCommandPool>(*secondaryCommandPool_) != VK_NULL_HANDLE);
    secondaryCommandPool_.reset();
}

const vk::raii::CommandBuffer& FrameInFlight::commandBuffer() const noexcept
{
    // Successful allocation returns exactly the one command buffer requested
    // above, so this container is never empty.
    return commandBuffers_.front();
}

const vk::raii::CommandBuffer& FrameInFlight::secondaryCommandBuffer() const noexcept
{
    assert(!secondaryCommandBuffers_.empty());
    return secondaryCommandBuffers_.front();
}

const vk::raii::Semaphore& FrameInFlight::imageAvailable() const noexcept
{
    return imageAvailable_;
}

const vk::raii::Fence& FrameInFlight::frameFinished() const noexcept
{
    return frameFinished_;
}

const AllocatedBuffer& FrameInFlight::uniformBuffer() const noexcept
{
    return uniformBuffer_;
}

void FrameInFlight::writeUniforms(const FrameUniforms& uniforms) const
{
    uniformBuffer_.write(std::as_bytes(std::span{&uniforms, 1}));
}
/** @endcond */
} // namespace fire_engine::detail
