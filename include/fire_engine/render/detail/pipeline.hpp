#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;

/* --- Classes --- */

/**
 * @brief Owns a pipeline layout and dynamic-rendering graphics pipeline.
 *
 * The pipeline targets one swapchain color format and one depth format without
 * using a render pass. Its set-zero layout accepts a uniform buffer and sampled
 * texture through Vulkan 1.4 push descriptors, so no descriptor pool or
 * allocated descriptor set is needed.
 *
 * The vertex input state describes interleaved mesh data. Set zero accepts the
 * frame's view-projection matrix, while push constants carry each draw's world
 * transform and material color.
 */
class Pipeline final
{
public:
    /**
     * @brief Creates the pipeline layout and graphics pipeline.
     * @param device Logical device with dynamic rendering, push descriptors, and
     *               maintenance5 enabled.
     * @param colorFormat Swapchain format used by the dynamic color attachment.
     * @param depthFormat Format used by the dynamic depth attachment.
     * @throws std::runtime_error if the compiled shader cannot be loaded.
     * @throws vk::SystemError if Vulkan cannot create a pipeline object.
     */
    Pipeline(const Device& device, vk::Format colorFormat, vk::Format depthFormat);

    /** @brief Releases the pipeline, pipeline layout, and retained set layout in order. */
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
     * @brief Returns the layout required by each push-descriptor write.
     * @return Reference to the owned pipeline layout.
     */
    [[nodiscard]] const vk::raii::PipelineLayout& pipelineLayout() const noexcept;

    /**
     * @brief Returns the graphics pipeline used by each draw command.
     * @return Reference to the owned graphics pipeline.
     */
    [[nodiscard]] const vk::raii::Pipeline& pipeline() const noexcept;

private:
    // Vulkan permits the original descriptor-set-layout handle to be destroyed
    // after pipeline-layout creation. Retaining it makes that construction
    // relationship explicit for the tutorial and avoids a validation-layer
    // lifetime diagnostic when push descriptors are recorded. Reverse member
    // destruction releases these objects in dependency order.
    vk::raii::DescriptorSetLayout descriptorSetLayout_{nullptr}; ///< Set-zero push layout.
    vk::raii::PipelineLayout pipelineLayout_{nullptr}; ///< Supplied to each push-descriptor write.
    vk::raii::Pipeline pipeline_{nullptr};             ///< Dynamic-rendering graphics pipeline.
};
/** @endcond */
} // namespace fire_engine::detail
