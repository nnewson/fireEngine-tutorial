#pragma once

#include <cstdint>

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;

/* --- Enums --- */

/** @brief Command-buffer allocation owned by one serial recording context. */
enum class RecordingBufferKind : std::uint8_t
{
    eNone,      ///< Own a resettable pool without allocating a command buffer.
    ePrimary,   ///< Allocate one primary command buffer.
    eSecondary, ///< Allocate one secondary command buffer.
};

/** @brief Temporary Step-8a control over the recording pool's lifetime hint. */
enum class RecordingPoolHint : std::uint8_t
{
    eNone,      ///< Create the pool without an allocation-lifetime hint.
    eTransient, ///< Declare the short-lived per-frame buffers with eTransient.
};

/* --- Classes --- */

/**
 * @brief Owns one externally synchronized command pool and optional buffer.
 *
 * A future worker receives only this recording-local state. It contains no
 * queue, submission fence, allocator, or frame-slot ownership.
 */
class RecordingContext final
{
public:
    /**
     * @brief Creates one recording-thread-local pool and requested command buffer.
     * @param device Logical device and graphics queue family used for allocation.
     * @param bufferKind Whether the context owns a primary, secondary, or no buffer.
     * @param poolHint Temporary Step-8a selection of the pool's lifetime hint.
     */
    RecordingContext(const Device& device, RecordingBufferKind bufferKind,
                     RecordingPoolHint poolHint);

    /** @brief Releases the command buffer before its pool. */
    ~RecordingContext() = default;

    /// @brief Copy construction is disabled because Vulkan handles have unique ownership.
    RecordingContext(const RecordingContext&) = delete;
    /// @brief Copy assignment is disabled because Vulkan handles have unique ownership.
    RecordingContext& operator=(const RecordingContext&) = delete;
    /// @brief Move construction is disabled so thread ownership remains explicit.
    RecordingContext(RecordingContext&&) = delete;
    /// @brief Move assignment is disabled so thread ownership remains explicit.
    RecordingContext& operator=(RecordingContext&&) = delete;

    /**
     * @brief Recycles all allocations made from this context's pool.
     * @pre Earlier submissions using its command buffers have completed.
     */
    void resetCommands() const;

    /**
     * @brief Returns the context's sole command buffer.
     * @pre The context was constructed with a non-eNone buffer kind.
     * @return Primary or secondary buffer allocated from this context's pool.
     */
    [[nodiscard]] const vk::raii::CommandBuffer& commandBuffer() const noexcept;

    /**
     * @brief Reports whether this context allocated a command buffer.
     * @return true for primary and secondary contexts; false for an empty control pool.
     */
    [[nodiscard]] bool hasCommandBuffer() const noexcept;

private:
    // Reverse destruction releases the buffer before the pool that allocated it.
    vk::raii::CommandPool commandPool_{nullptr};       ///< Pool owned by one recording thread.
    vk::raii::CommandBuffers commandBuffers_{nullptr}; ///< Zero or one command buffer.
};
/** @endcond */
} // namespace fire_engine::detail
