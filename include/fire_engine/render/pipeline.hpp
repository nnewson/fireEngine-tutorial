#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class Device;

/* --- Classes --- */

/**
 * @brief Owns a pipeline layout and dynamic-rendering graphics pipeline.
 *
 * The pipeline targets one swapchain color format and does not use a render
 * pass. Its set-zero layout accepts a uniform buffer through Vulkan 1.4 push
 * descriptors, so no descriptor pool or allocated descriptor set is needed.
 *
 * This milestone builds the pipeline but cannot yet draw with it. The vertex
 * input state and the set-zero binding describe a vertex buffer and a per-frame
 * uniform buffer that later milestones allocate, upload, and bind.
 */
class Pipeline final
{
public:
    /**
     * @brief Creates the pipeline layout and graphics pipeline.
     * @param device Logical device with dynamic rendering, push descriptors, and
     *               maintenance5 enabled.
     * @param colorFormat Swapchain format used by the dynamic color attachment.
     * @throws std::runtime_error if the compiled shader cannot be loaded.
     * @throws vk::SystemError if Vulkan cannot create a pipeline object.
     */
    Pipeline(const Device& device, vk::Format colorFormat);

    /** @brief Releases the pipeline and then the layout that recording still needs. */
    ~Pipeline() = default;

    /// @brief Copy construction is disabled because Vulkan handles have unique ownership.
    Pipeline(const Pipeline&) = delete;
    /// @brief Copy assignment is disabled because Vulkan handles have unique ownership.
    Pipeline& operator=(const Pipeline&) = delete;
    /// @brief Move construction is disabled so the pipeline lifetime remains explicit.
    Pipeline(Pipeline&&) = delete;
    /// @brief Move assignment is disabled so the pipeline lifetime remains explicit.
    Pipeline& operator=(Pipeline&&) = delete;

    /**
     * @brief Returns the layout required by future push-descriptor writes.
     * @return Reference to the owned pipeline layout.
     */
    [[nodiscard]] const vk::raii::PipelineLayout& pipelineLayout() const noexcept;

    /**
     * @brief Returns the graphics pipeline used by future draw commands.
     * @return Reference to the owned graphics pipeline.
     */
    [[nodiscard]] const vk::raii::Pipeline& pipeline() const noexcept;

    // Only two of the three objects created by the constructor are kept. The
    // descriptor-set layout describing set zero is consumed by pipeline-layout
    // creation and never referenced again, so the constructor lets it go. Push
    // descriptors allocate no descriptor sets, which is what would otherwise
    // require that layout to stay alive.
    //
    // The pipeline layout is different: pushDescriptorSet takes it on every
    // recorded frame, so it must outlive construction. Vulkan would allow it to
    // be destroyed once the pipeline exists, so the declaration order below
    // reflects that use, not a creation dependency.
private:
    vk::raii::PipelineLayout pipelineLayout_{nullptr}; ///< Supplied to each push-descriptor write.
    vk::raii::Pipeline pipeline_{nullptr};             ///< Dynamic-rendering graphics pipeline.
};
} // namespace fire_engine
