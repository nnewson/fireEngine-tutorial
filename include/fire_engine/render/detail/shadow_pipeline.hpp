#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <fire_engine/graphics/pipeline_description.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;

/* --- Classes --- */

/** @brief Owns the depth-only directional-shadow pipeline and its layout. */
class ShadowPipeline final
{
public:
    /**
     * @brief Creates the depth-only dynamic-rendering pipeline.
     * @param device Logical device with dynamic rendering, push descriptors, and maintenance5.
     * @param description Vulkan-free vertex compatibility requirements.
     * @param depthFormat Sampled depth-attachment format selected for every shadow map.
     * @throws std::runtime_error if the compiled shader cannot be loaded.
     * @throws vk::SystemError if Vulkan cannot create a pipeline object.
     */
    ShadowPipeline(const Device& device, PipelineDescription description, vk::Format depthFormat);

    /** @brief Releases the pipeline, pipeline layout, and retained set layout in order. */
    ~ShadowPipeline() = default;

    ShadowPipeline(const ShadowPipeline&) = delete;
    ShadowPipeline& operator=(const ShadowPipeline&) = delete;
    ShadowPipeline(ShadowPipeline&&) = delete;
    ShadowPipeline& operator=(ShadowPipeline&&) = delete;

    /** @brief Returns the set-zero pipeline layout. @return Owned pipeline layout. */
    [[nodiscard]] const vk::raii::PipelineLayout& pipelineLayout() const noexcept;
    /** @brief Returns the depth-only graphics pipeline. @return Owned graphics pipeline. */
    [[nodiscard]] const vk::raii::Pipeline& pipeline() const noexcept;
    /** @brief Returns the compiled vertex compatibility description. @return Description. */
    [[nodiscard]] const PipelineDescription& description() const noexcept;

private:
    // Retain the push-descriptor layout beside its dependent pipeline layout,
    // matching ForwardPipeline's explicit construction relationship.
    PipelineDescription description_; ///< Vertex layout compiled into fixed pipeline state.
    vk::raii::DescriptorSetLayout descriptorSetLayout_{nullptr}; ///< Set-zero push layout.
    vk::raii::PipelineLayout pipelineLayout_{nullptr}; ///< Uniform and push-constant interface.
    vk::raii::Pipeline pipeline_{nullptr};             ///< Depth-only graphics pipeline.
};
/** @endcond */
} // namespace fire_engine::detail
