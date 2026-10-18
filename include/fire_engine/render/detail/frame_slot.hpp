#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;

/* --- Classes --- */

/**
 * @brief Owns the acquisition semaphore and pending-work state reused as one frame slot.
 *
 * Slot retirement is not stored here: the renderer waits on its frame timeline
 * for the slot's last submission serial, which detail::SubmissionSerials records.
 */
class FrameSlot final
{
public:
    /**
     * @brief Creates the acquisition semaphore for one submission slot.
     * @param device Logical device used to create the semaphore.
     */
    explicit FrameSlot(const Device& device);

    /** @brief Releases synchronization state. */
    ~FrameSlot() = default;

    /// @brief Copy construction is disabled because Vulkan handles have unique ownership.
    FrameSlot(const FrameSlot&) = delete;
    /// @brief Copy assignment is disabled because Vulkan handles have unique ownership.
    FrameSlot& operator=(const FrameSlot&) = delete;
    /// @brief Move construction is disabled so frame ownership remains explicit.
    FrameSlot(FrameSlot&&) = delete;
    /// @brief Move assignment is disabled so frame ownership remains explicit.
    FrameSlot& operator=(FrameSlot&&) = delete;

    /** @brief Returns the acquisition semaphore. @return Initially unsignaled semaphore. */
    [[nodiscard]] const vk::raii::Semaphore& imageAvailable() const noexcept;
    /**
     * @brief Reports whether submitted work may still use slot-owned resources.
     *
     * This is conservative cleanup state, not command completion. Only a full
     * device and presentation wait clears it: a timeline value showing that the
     * commands finished says nothing about presentation still using resources.
     *
     * @return true after submission until a device and presentation wait clears it.
     */
    [[nodiscard]] bool workMayBePending() const noexcept;
    /** @brief Marks the slot as potentially referenced by submitted work. */
    void markWorkPending() noexcept;
    /** @brief Clears pending-work state after a device completion wait. */
    void clearPendingWork() noexcept;

private:
    vk::raii::Semaphore imageAvailable_{nullptr}; ///< Signals image acquisition.
    bool workMayBePending_ = false;               ///< Whether defensive retirement must wait.
};
/** @endcond */
} // namespace fire_engine::detail
