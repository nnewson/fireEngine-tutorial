#include <fire_engine/render/detail/frame_in_flight.hpp>

#include <fire_engine/render/detail/allocator.hpp>
#include <fire_engine/render/detail/device.hpp>

#include <span>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

FrameInFlight::FrameInFlight(const Device& device, const MemoryAllocator& allocator,
                             const FrameUniforms& initialUniforms)
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

    // A primary command buffer can be submitted directly to the graphics
    // queue. Secondary command buffers are instead executed from a primary one
    // and would add indirection that a single-triangle tutorial does not need.
    const vk::CommandBufferAllocateInfo commandBufferInfo{
        .commandPool = *commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    commandBuffers_ = vk::raii::CommandBuffers{device.logicalDevice(), commandBufferInfo};

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

void FrameInFlight::resetCommands() const
{
    commandPool_.reset();
}

const vk::raii::CommandBuffer& FrameInFlight::commandBuffer() const noexcept
{
    // Successful allocation returns exactly the one command buffer requested
    // above, so this container is never empty.
    return commandBuffers_.front();
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
/** @endcond */
} // namespace fire_engine::detail
