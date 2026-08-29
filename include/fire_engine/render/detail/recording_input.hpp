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

/** @brief Fixed plain-handle state required by an independent recording context. */
struct RecordingState
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

/** @brief One fully resolved draw packet copied into the recording-input arena. */
struct RecordingDraw
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
 * @brief Immutable capability view consumed during one CPU recording transaction.
 *
 * The input contains plain Vulkan handles rather than RAII owners. Its draw
 * span remains valid until RecordingInputCompiler compiles another input.
 * Renderer creates it inside one non-reentrant drawFrame transaction, keeps
 * every referenced owner stable until all synchronous CPU recording consumers
 * have returned, and joins any future internal workers before the transaction
 * continues. Normal submission retirement protects resources subsequently
 * consumed by the GPU.
 */
class RecordingInput final
{
public:
    /** @brief Ends the non-owning recording-input transaction. */
    ~RecordingInput() = default;

    RecordingInput(const RecordingInput&) = delete;
    RecordingInput& operator=(const RecordingInput&) = delete;
    RecordingInput(RecordingInput&&) = delete;
    RecordingInput& operator=(RecordingInput&&) = delete;

    /** @brief Returns fixed worker-visible state. @return Plain handles and dynamic state. */
    [[nodiscard]] const RecordingState& state() const noexcept;

    /** @brief Returns ordered compiled packets. @return Read-only arena-backed draw span. */
    [[nodiscard]] std::span<const RecordingDraw> draws() const noexcept;

private:
    friend class RecordingInputCompiler;

    /**
     * @brief Freezes one complete worker-visible input.
     * @param state Fixed plain-handle recording state.
     * @param draws Compiled packets owned by the compiler until its next build.
     */
    RecordingInput(RecordingState state, std::span<const RecordingDraw> draws) noexcept;

    RecordingState state_;                 ///< Complete fixed recording capability.
    std::span<const RecordingDraw> draws_; ///< Immutable packets in stable arena storage.
};

/** @brief Resolves external scene draws into one immutable recording-input transaction. */
class RecordingInputCompiler final
{
public:
    /** @brief Creates an empty reusable compiled-packet arena. */
    RecordingInputCompiler() = default;
    /** @brief Releases packet storage after the active input has expired. */
    ~RecordingInputCompiler() = default;

    RecordingInputCompiler(const RecordingInputCompiler&) = delete;
    RecordingInputCompiler& operator=(const RecordingInputCompiler&) = delete;
    RecordingInputCompiler(RecordingInputCompiler&&) = delete;
    RecordingInputCompiler& operator=(RecordingInputCompiler&&) = delete;

    /**
     * @brief Resolves, validates, and freezes every draw for one recording transaction.
     * @param drawList External immutable scene snapshot consumed only during this call.
     * @param resources Restricted lookup into the current compiled-resource generation.
     * @param state Current presentation and frame-slot recording state.
     * @return Immutable input valid until this compiler is used again.
     * @throws std::logic_error if a draw was not prepared or is pipeline-incompatible.
     * @pre The previous input returned by this compiler has no remaining CPU consumers.
     */
    [[nodiscard]] RecordingInput compile(const SceneDrawList& drawList,
                                         CompiledResourcesView resources, RecordingState state);

private:
    std::vector<RecordingDraw> draws_; ///< Reused high-water packet storage.
};
/** @endcond */
} // namespace detail
} // namespace fire_engine
