#pragma once

#include <vulkan/vulkan.hpp>

namespace fire_engine::detail
{
/* --- POD structs --- */

/** @brief Binding commands required before one compiled draw. */
struct DrawBindingChanges
{
    bool geometry = false; ///< Whether vertex and index buffers must be rebound.
    bool texture = false;  ///< Whether the sampled-image descriptor must be rewritten.

    /** @brief Compares the required binding commands. @return True when both flags match. */
    [[nodiscard]] constexpr bool operator==(const DrawBindingChanges&) const noexcept = default;
};

/* --- Classes --- */

/**
 * @brief Tracks bindings already emitted after establishing complete geometry state.
 *
 * The cache is valid only while descriptor set zero remains compatible with the
 * established pipeline layout. Re-establish the complete geometry state and create
 * a new cache after binding an incompatible pipeline or after a descriptor-set bind
 * disturbs set zero.
 */
class DrawBindingState final
{
public:
    /** @brief Creates an empty cache for a newly established geometry state. */
    DrawBindingState() = default;

    /** @brief Releases the command-buffer-local cache. */
    ~DrawBindingState() = default;

    /// @brief Copy construction is disabled because cached bindings belong to one command buffer.
    DrawBindingState(const DrawBindingState&) = delete;
    /// @brief Copy assignment is disabled because cached bindings belong to one command buffer.
    DrawBindingState& operator=(const DrawBindingState&) = delete;

    /** @brief Transfers a command-buffer-local cache without duplicating it. */
    DrawBindingState(DrawBindingState&&) noexcept = default;
    /**
     * @brief Transfers a command-buffer-local cache without duplicating it.
     * @return This cache containing the transferred state.
     */
    DrawBindingState& operator=(DrawBindingState&&) noexcept = default;

    /**
     * @brief Updates the cached binding keys for the next draw.
     * @param vertexBuffer Vertex buffer required by the draw.
     * @param indexBuffer Index buffer required by the draw.
     * @param sampler Sampler required by the draw's material.
     * @param imageView Sampled image view required by the draw's material.
     * @return Which command-buffer bindings differ from the previous draw.
     */
    [[nodiscard]] DrawBindingChanges update(vk::Buffer vertexBuffer, vk::Buffer indexBuffer,
                                            vk::Sampler sampler, vk::ImageView imageView) noexcept;

private:
    vk::Buffer vertexBuffer_;      ///< Last vertex buffer emitted into this command buffer.
    vk::Buffer indexBuffer_;       ///< Last index buffer emitted into this command buffer.
    vk::Sampler sampler_;          ///< Last sampled-image sampler pushed into this command buffer.
    vk::ImageView imageView_;      ///< Last sampled-image view pushed into this command buffer.
    bool hasPreviousDraw_ = false; ///< Whether the keys describe an emitted draw binding.
};
} // namespace fire_engine::detail
