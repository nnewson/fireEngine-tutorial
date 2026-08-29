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

/** @brief Owns CPU/GPU submission state reused as one frame slot. */
class FrameSlot final
{
public:
    /**
     * @brief Creates the uniform storage and synchronization for one submission slot.
     * @param device Logical device used to create synchronization objects.
     * @param allocator VMA owner used for the uniform buffer.
     * @param initialUniforms Defensive shader values written before construction completes;
     * Renderer overwrites them after slot retirement and before every submission.
     */
    FrameSlot(const Device& device, const MemoryAllocator& allocator,
              const FrameUniforms& initialUniforms);

    /** @brief Releases uniform and synchronization state. */
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
    /** @brief Returns the submission fence. @return Initially signaled fence. */
    [[nodiscard]] const vk::raii::Fence& frameFinished() const noexcept;
    /** @brief Returns the slot-local uniform buffer. @return Host-populated uniform storage. */
    [[nodiscard]] const AllocatedBuffer& uniformBuffer() const noexcept;
    /** @brief Replaces the slot-local uniform values. @param uniforms New shader values. */
    void writeUniforms(const FrameUniforms& uniforms) const;

    /**
     * @brief Reports whether submitted work may still use slot-owned resources.
     * @return true after submission until the renderer completes its retirement wait.
     */
    [[nodiscard]] bool workMayBePending() const noexcept;
    /** @brief Marks the slot as potentially referenced by submitted work. */
    void markWorkPending() noexcept;
    /** @brief Clears pending-work state after a device completion wait. */
    void clearPendingWork() noexcept;

private:
    AllocatedBuffer uniformBuffer_;               ///< Shader values belonging to this slot.
    vk::raii::Semaphore imageAvailable_{nullptr}; ///< Signals image acquisition.
    vk::raii::Fence frameFinished_{nullptr};      ///< Signals submission completion.
    bool workMayBePending_ = false;               ///< Whether defensive retirement must wait.
};
/** @endcond */
} // namespace fire_engine::detail
