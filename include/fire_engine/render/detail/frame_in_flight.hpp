#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <fire_engine/render/detail/buffer.hpp>
#include <fire_engine/render/detail/frame_uniforms.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;
class MemoryAllocator;

/* --- Classes --- */

/**
 * @brief Preserves the legacy combined frame-and-recording ownership control.
 *
 * The current serial implementation combines one submission slot with one
 * recording context: a primary command buffer owns the frame boundaries, a
 * secondary records the dynamic-rendering geometry pass, a semaphore orders
 * image acquisition, a fence reports submission completion, and a uniform
 * buffer holds values that may change after that fence signals.
 *
 * This complete pre-Step-4 arrangement remains temporarily selectable so its
 * one-thread cost can be measured beside the separated ownership path in the
 * same binary and session. Production ownership is FrameSlot plus independent
 * RecordingContext objects.
 *
 * The matching render-finished semaphores are deliberately not here. Those are
 * indexed by swapchain image, so Swapchain owns them; see its documentation for
 * why presentation semaphores must follow images instead of frames.
 */
class FrameInFlight final
{
public:
    /**
     * @brief Creates the command buffers and this frame's synchronization state.
     * @param device Logical device and graphics queue family used by this frame.
     * @param allocator VMA owner used for the frame's uniform buffer.
     * @param initialUniforms Initial shader values written before construction completes.
     * @throws vk::SystemError if Vulkan cannot create or allocate an object.
     * @throws std::runtime_error if VMA cannot create or populate the uniform buffer.
     */
    FrameInFlight(const Device& device, const MemoryAllocator& allocator,
                  const FrameUniforms& initialUniforms);

    /** @brief Releases the uniform, synchronization objects, command buffers, and pool. */
    ~FrameInFlight() = default;

    /// @brief Copy construction is disabled because Vulkan handles have unique ownership.
    FrameInFlight(const FrameInFlight&) = delete;
    /// @brief Copy assignment is disabled because Vulkan handles have unique ownership.
    FrameInFlight& operator=(const FrameInFlight&) = delete;
    /// @brief Move construction is disabled so frame ownership remains explicit.
    FrameInFlight(FrameInFlight&&) = delete;
    /// @brief Move assignment is disabled so frame ownership remains explicit.
    FrameInFlight& operator=(FrameInFlight&&) = delete;

    /**
     * @brief Recycles the pool so its command buffers can be recorded again.
     *
     * The previous submission must have completed first, which the frame loop
     * guarantees by waiting on frameFinished(). Resetting a pool whose commands
     * are still executing is undefined behavior.
     *
     * Resetting the pool returns every command buffer allocated from it to the
     * initial state. The pool is created without eResetCommandBuffer, so this is
     * the only legal way to re-record: resetting the buffer on its own, or
     * relying on begin() to reset it implicitly, both require that flag.
     *
     * This is const because what changes lives in driver memory rather than in
     * this object, the same reason Vulkan-Hpp declares the underlying pool reset
     * const. That carries no thread-safety promise: Vulkan requires a command
     * pool to be externally synchronized against reset and recording alike.
     *
     * @throws vk::SystemError if the device cannot reset the pool.
     */
    void resetCommands() const;

    /**
     * @brief Returns the primary command buffer reused for every frame.
     * @return Reference to the command buffer allocated from the graphics pool.
     */
    [[nodiscard]] const vk::raii::CommandBuffer& commandBuffer() const noexcept;

    /**
     * @brief Returns the secondary command buffer executed by the primary geometry pass.
     * @return Reference to the command buffer allocated from the graphics pool.
     */
    [[nodiscard]] const vk::raii::CommandBuffer& secondaryCommandBuffer() const noexcept;

    /**
     * @brief Returns the semaphore signaled when a swapchain image is acquired.
     * @return Reference to the frame's image-available binary semaphore.
     */
    [[nodiscard]] const vk::raii::Semaphore& imageAvailable() const noexcept;

    /**
     * @brief Returns the fence that marks completion of the frame submission.
     *
     * The frame loop waits on this, then resets it before submitting again.
     * Reset it only once the frame is certain to submit, which means acquiring
     * the swapchain image first: abandoning a frame on an out-of-date swapchain
     * after the reset leaves an unsignaled fence that nothing will ever signal,
     * and the next iteration waits on it forever.
     *
     * @return Reference to the initially signaled frame fence.
     */
    [[nodiscard]] const vk::raii::Fence& frameFinished() const noexcept;

    /**
     * @brief Returns the uniform buffer pushed into descriptor set zero for this frame.
     * @return Host-populated buffer containing one FrameUniforms value.
     */
    [[nodiscard]] const AllocatedBuffer& uniformBuffer() const noexcept;

    /**
     * @brief Replaces the frame-uniform contents after earlier frame work has completed.
     * @param uniforms New shader values copied into the persistently owned buffer.
     * @throws std::runtime_error if VMA cannot map or write the allocation.
     */
    void writeUniforms(const FrameUniforms& uniforms) const;

private:
    AllocatedBuffer uniformBuffer_; ///< Shader data kept separate for each frame slot.

    // Members are destroyed in reverse declaration order. Command buffers must
    // therefore follow the pool from which they were allocated so they are
    // freed before that pool is destroyed.
    vk::raii::CommandPool commandPool_{nullptr};       ///< Graphics pool recycled by resetCommands.
    vk::raii::CommandBuffers commandBuffers_{nullptr}; ///< Contains one primary command buffer.
    vk::raii::CommandBuffers secondaryCommandBuffers_{nullptr}; ///< Contains one secondary buffer.
    vk::raii::Semaphore imageAvailable_{nullptr}; ///< Signals swapchain-image acquisition.
    vk::raii::Fence frameFinished_{nullptr};      ///< Signals completion of submitted work.
};
/** @endcond */
} // namespace fire_engine::detail
