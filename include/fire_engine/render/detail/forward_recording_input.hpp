#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <fire_engine/graphics/pipeline_description.hpp>
#include <fire_engine/render/detail/compiled_resources.hpp>
#include <fire_engine/render/detail/draw_constants.hpp>
#include <fire_engine/render/detail/frame_uniforms.hpp>

namespace fire_engine
{
struct SceneDrawList;

namespace detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Fixed plain-handle state required by one forward recording context. */
struct ForwardRecordingState
{
    vk::Pipeline pipeline;             ///< Graphics pipeline compatible with the attachments.
    vk::PipelineLayout pipelineLayout; ///< Layout used by descriptors and push constants.
    vk::Buffer frameUniformBuffer;     ///< Slot-local world-to-clip uniform storage.
    FrameUniforms frameUniforms;       ///< Per-frame values written after slot retirement.
    vk::Viewport viewport;             ///< Complete dynamic viewport, including the Y flip.
    vk::Rect2D scissor;                ///< Complete dynamic scissor for this presentation extent.
    vk::Format colorAttachmentFormat;  ///< Secondary rendering-inheritance color format.
    vk::Format depthAttachmentFormat;  ///< Secondary rendering-inheritance depth format.
    VertexLayoutKey vertexLayout;      ///< Layout compiled into pipeline vertex input state.
};

/** @brief One fully resolved forward draw copied into the recording-input arena. */
struct ForwardRecordingDraw
{
    vk::Buffer vertexBuffer;  ///< Device-local vertex buffer.
    vk::Buffer indexBuffer;   ///< Device-local 32-bit index buffer.
    std::uint32_t indexCount; ///< Number of indices consumed by drawIndexed.
    vk::Sampler sampler;      ///< Immutable texture sampler handle.
    vk::ImageView imageView;  ///< Immutable sampled-image view handle.
    DrawConstants constants;  ///< Model transform and material values pushed for this draw.
};

/* --- Classes --- */

/**
 * @brief Immutable forward capability consumed during one CPU recording transaction.
 *
 * The input contains plain Vulkan handles rather than RAII owners. Its draw
 * span remains valid until ForwardRecordingInputCompiler compiles another input.
 * Renderer creates it inside one non-reentrant drawFrame transaction, keeps
 * every referenced owner stable until all synchronous CPU recording consumers
 * have returned, and joins every internal worker before the transaction
 * continues. Normal submission retirement protects resources subsequently
 * consumed by the GPU.
 */
class ForwardRecordingInput final
{
public:
    /** @brief Ends the non-owning forward-recording transaction. */
    ~ForwardRecordingInput() = default;

    ForwardRecordingInput(const ForwardRecordingInput&) = delete;
    ForwardRecordingInput& operator=(const ForwardRecordingInput&) = delete;
    ForwardRecordingInput(ForwardRecordingInput&&) = delete;
    ForwardRecordingInput& operator=(ForwardRecordingInput&&) = delete;

    /** @brief Returns fixed worker-visible state. @return Plain handles and dynamic state. */
    [[nodiscard]] const ForwardRecordingState& state() const noexcept;

    /** @brief Returns ordered compiled packets. @return Read-only arena-backed draw span. */
    [[nodiscard]] std::span<const ForwardRecordingDraw> draws() const noexcept;

private:
    friend class ForwardRecordingInputCompiler;

    /**
     * @brief Freezes one complete worker-visible input.
     * @param state Fixed plain-handle forward recording state.
     * @param draws Compiled packets owned by the compiler until its next build.
     */
    ForwardRecordingInput(ForwardRecordingState state,
                          std::span<const ForwardRecordingDraw> draws) noexcept;

    ForwardRecordingState state_;                 ///< Complete fixed recording capability.
    std::span<const ForwardRecordingDraw> draws_; ///< Immutable packets in stable arena storage.
};

/** @brief Resolves external scene draws into one immutable forward-recording transaction. */
class ForwardRecordingInputCompiler final
{
public:
    /** @brief Creates an empty reusable compiled-packet arena. */
    ForwardRecordingInputCompiler() = default;
    /** @brief Releases packet storage after the active input has expired. */
    ~ForwardRecordingInputCompiler() = default;

    ForwardRecordingInputCompiler(const ForwardRecordingInputCompiler&) = delete;
    ForwardRecordingInputCompiler& operator=(const ForwardRecordingInputCompiler&) = delete;
    ForwardRecordingInputCompiler(ForwardRecordingInputCompiler&&) = delete;
    ForwardRecordingInputCompiler& operator=(ForwardRecordingInputCompiler&&) = delete;

    /**
     * @brief Resolves, validates, and freezes every draw for one recording transaction.
     * @param drawList External immutable scene snapshot consumed only during this call.
     * @param resources Restricted lookup into the current compiled-resource generation.
     * @param state Current presentation and frame-slot forward recording state.
     * @return Immutable input valid until this compiler is used again.
     * @throws std::logic_error if a draw was not prepared or is pipeline-incompatible.
     * @pre The previous input returned by this compiler has no remaining CPU consumers.
     */
    [[nodiscard]] ForwardRecordingInput compile(const SceneDrawList& drawList,
                                                CompiledResourcesView resources,
                                                ForwardRecordingState state);

private:
    std::vector<ForwardRecordingDraw> draws_; ///< Reused high-water packet storage.
};
/** @endcond */
} // namespace detail
} // namespace fire_engine
