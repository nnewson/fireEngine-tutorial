#pragma once

#include <vulkan/vulkan.hpp>

#include <fire_engine/render/detail/buffer.hpp>
#include <fire_engine/render/detail/frame_uniforms.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class MemoryAllocator;

/* --- Classes --- */

/**
 * @brief Owns shared frame-uniform storage belonging to one reusable frame slot.
 *
 * The buffer survives presentation recreation and is selected by frame-slot
 * index rather than acquired-image index. The renderer updates it only after
 * the matching slot fence retires and before that slot is submitted again.
 */
class FrameUniformBuffer final
{
public:
    /**
     * @brief Creates the buffer and initializes it with defensive identity values.
     * @param allocator VMA owner that must outlive this buffer.
     */
    explicit FrameUniformBuffer(const MemoryAllocator& allocator);

    /** @brief Releases the uniform buffer and its allocation together. */
    ~FrameUniformBuffer() = default;

    /// @brief Copy construction is disabled because the allocation has one owner.
    FrameUniformBuffer(const FrameUniformBuffer&) = delete;
    /// @brief Copy assignment is disabled because the allocation has one owner.
    FrameUniformBuffer& operator=(const FrameUniformBuffer&) = delete;
    /// @brief Move construction is disabled so frame-slot ownership remains explicit.
    FrameUniformBuffer(FrameUniformBuffer&&) = delete;
    /// @brief Move assignment is disabled so frame-slot ownership remains explicit.
    FrameUniformBuffer& operator=(FrameUniformBuffer&&) = delete;

    /** @brief Returns the slot-local Vulkan buffer. @return Non-owning uniform-buffer handle. */
    [[nodiscard]] vk::Buffer handle() const noexcept;

    /** @brief Replaces the slot-local shared values. @param uniforms New shader values. */
    void update(const FrameUniforms& uniforms) const;

private:
    AllocatedBuffer buffer_; ///< Host-visible shared values for one submission slot.
};
/** @endcond */
} // namespace fire_engine::detail
