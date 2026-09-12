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
 * @brief Owns forward uniform storage belonging to one reusable frame slot.
 *
 * The buffer survives presentation recreation and is selected by frame-slot
 * index rather than acquired-image index. The renderer updates it only after
 * the matching slot fence retires and before that slot is submitted again.
 */
class ForwardFrameUniformBuffer final
{
public:
    /**
     * @brief Creates the buffer and initializes it with defensive identity values.
     * @param allocator VMA owner that must outlive this buffer.
     */
    explicit ForwardFrameUniformBuffer(const MemoryAllocator& allocator);

    /** @brief Releases the uniform buffer and its allocation together. */
    ~ForwardFrameUniformBuffer() = default;

    /// @brief Copy construction is disabled because the allocation has one owner.
    ForwardFrameUniformBuffer(const ForwardFrameUniformBuffer&) = delete;
    /// @brief Copy assignment is disabled because the allocation has one owner.
    ForwardFrameUniformBuffer& operator=(const ForwardFrameUniformBuffer&) = delete;
    /// @brief Move construction is disabled so frame-slot ownership remains explicit.
    ForwardFrameUniformBuffer(ForwardFrameUniformBuffer&&) = delete;
    /// @brief Move assignment is disabled so frame-slot ownership remains explicit.
    ForwardFrameUniformBuffer& operator=(ForwardFrameUniformBuffer&&) = delete;

    /** @brief Returns the slot-local Vulkan buffer. @return Non-owning uniform-buffer handle. */
    [[nodiscard]] vk::Buffer handle() const noexcept;

    /** @brief Replaces the slot-local forward values. @param uniforms New shader values. */
    void update(const FrameUniforms& uniforms) const;

private:
    AllocatedBuffer buffer_; ///< Host-visible forward values for one submission slot.
};
/** @endcond */
} // namespace fire_engine::detail
