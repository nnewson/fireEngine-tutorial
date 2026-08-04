#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class Device;

/* --- Classes --- */

/**
 * @brief Owns the command and synchronization objects for one frame in flight.
 *
 * Everything here is indexed by frame rather than by swapchain image: one
 * primary command buffer that is recorded again for each frame, the semaphore
 * that orders image acquisition before rendering, and the fence that tells the
 * CPU when the frame's submitted work has finished. Going from one frame in
 * flight to several means creating one instance per frame slot and cycling
 * through them, with nothing here needing to change. A std::array holds them
 * well: the count is known at compile time, and guaranteed copy elision
 * constructs the elements in place despite this type being immovable.
 *
 * The matching render-finished semaphores are deliberately not here. Those are
 * indexed by swapchain image, so Swapchain owns them; see its documentation for
 * why presentation semaphores must follow images instead of frames.
 */
class FrameInFlight final
{
public:
    /**
     * @brief Creates one command buffer and this frame's synchronization state.
     * @param device Logical device and graphics queue family used by this frame.
     * @throws vk::SystemError if Vulkan cannot create or allocate an object.
     */
    explicit FrameInFlight(const Device& device);

    /** @brief Releases synchronization objects, the command buffer, and its pool. */
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
     * @brief Recycles the pool so its command buffer can be recorded again.
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
     * Nothing calls this until command recording arrives in the next stage.
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

private:
    // Members are destroyed in reverse declaration order. Command buffers must
    // therefore follow the pool from which they were allocated so they are
    // freed before that pool is destroyed.
    vk::raii::CommandPool commandPool_{nullptr};       ///< Graphics pool recycled by resetCommands.
    vk::raii::CommandBuffers commandBuffers_{nullptr}; ///< Contains one primary command buffer.
    vk::raii::Semaphore imageAvailable_{nullptr};      ///< Signals swapchain-image acquisition.
    vk::raii::Fence frameFinished_{nullptr};           ///< Signals completion of submitted work.
};
} // namespace fire_engine
