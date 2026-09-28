#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <fire_engine/graphics/pipeline_description.hpp>
#include <fire_engine/render/detail/compiled_resources.hpp>
#include <fire_engine/render/detail/draw_constants.hpp>
#include <fire_engine/render/detail/frame_uniforms.hpp>
#include <fire_engine/render/detail/shadow_draw_constants.hpp>

namespace fire_engine
{
struct SceneDrawList;

namespace detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Fixed plain-handle state required by the depth-only shadow recorder. */
struct ShadowRecordingState
{
    vk::Pipeline pipeline;             ///< Depth-only graphics pipeline.
    vk::PipelineLayout pipelineLayout; ///< Layout used by uniform and model writes.
    vk::Buffer frameUniformBuffer;     ///< Shared slot-local frame-uniform storage.
    vk::Format depthAttachmentFormat;  ///< Format selected for every shadow map.
    VertexLayoutKey vertexLayout;      ///< Layout compiled into pipeline vertex input state.
};

/** @brief One fully resolved shadow draw copied into the frame-input arena. */
struct ShadowRecordingDraw
{
    vk::Buffer vertexBuffer;       ///< Device-local vertex buffer.
    vk::Buffer indexBuffer;        ///< Device-local 32-bit index buffer.
    std::uint32_t indexCount;      ///< Number of indices consumed by drawIndexed.
    ShadowDrawConstants constants; ///< Object-to-world transform pushed for this caster.
};

/** @brief Fixed plain-handle state required by one forward recording context. */
struct ForwardRecordingState
{
    vk::Pipeline pipeline;               ///< Graphics pipeline compatible with the attachments.
    vk::PipelineLayout pipelineLayout;   ///< Layout used by descriptors and push constants.
    vk::Buffer frameUniformBuffer;       ///< Shared slot-local frame-uniform storage.
    vk::ImageView shadowMapView;         ///< Sampled depth-only view from this frame slot.
    vk::Sampler shadowComparisonSampler; ///< Renderer-lifetime depth comparison sampler.
    vk::Viewport viewport;               ///< Complete dynamic viewport, including the Y flip.
    vk::Rect2D scissor;                  ///< Complete dynamic scissor for this presentation extent.
    vk::Format colorAttachmentFormat;    ///< Secondary rendering-inheritance color format.
    vk::Format depthAttachmentFormat;    ///< Secondary rendering-inheritance depth format.
    VertexLayoutKey vertexLayout;        ///< Layout compiled into pipeline vertex input state.
};

/** @brief One fully resolved forward draw copied into the frame-input arena. */
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

/** @brief Immutable shadow capability borrowed from one complete frame input. */
class ShadowRecordingInput final
{
public:
    /** @brief Ends the non-owning shadow-recording view. */
    ~ShadowRecordingInput() = default;

    ShadowRecordingInput(const ShadowRecordingInput&) = delete;
    ShadowRecordingInput& operator=(const ShadowRecordingInput&) = delete;
    ShadowRecordingInput(ShadowRecordingInput&&) = delete;
    ShadowRecordingInput& operator=(ShadowRecordingInput&&) = delete;

    /** @brief Returns fixed shadow state. @return Plain pipeline and uniform handles. */
    [[nodiscard]] const ShadowRecordingState& state() const noexcept;
    /** @brief Returns ordered caster packets. @return Read-only arena-backed draw span. */
    [[nodiscard]] std::span<const ShadowRecordingDraw> draws() const noexcept;

private:
    friend class FrameRecordingInput;

    /**
     * @brief Freezes one restricted shadow view.
     * @param state Fixed plain-handle shadow recording state.
     * @param draws Filtered packets owned by the compiler until its next build.
     */
    ShadowRecordingInput(ShadowRecordingState state,
                         std::span<const ShadowRecordingDraw> draws) noexcept;

    ShadowRecordingState state_;                 ///< Complete restricted shadow capability.
    std::span<const ShadowRecordingDraw> draws_; ///< Filtered packets in stable arena storage.
};

/** @brief Immutable forward capability borrowed from one complete frame input. */
class ForwardRecordingInput final
{
public:
    /** @brief Ends the non-owning forward-recording view. */
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
    friend class FrameRecordingInput;

    /**
     * @brief Freezes one restricted forward view.
     * @param state Fixed plain-handle forward recording state.
     * @param draws Complete packets owned by the compiler until its next build.
     */
    ForwardRecordingInput(ForwardRecordingState state,
                          std::span<const ForwardRecordingDraw> draws) noexcept;

    ForwardRecordingState state_;                 ///< Complete restricted forward capability.
    std::span<const ForwardRecordingDraw> draws_; ///< All packets in stable arena storage.
};

/**
 * @brief One immutable frame transaction containing shared values and restricted pass views.
 *
 * Both draw spans remain valid until FrameRecordingInputCompiler compiles another input. The
 * renderer keeps every referenced owner stable until synchronous CPU recording has returned and
 * joins every internal worker before ending the transaction. Normal submission retirement then
 * protects the resources consumed by the GPU.
 */
class FrameRecordingInput final
{
public:
    /** @brief Ends the complete non-owning frame-recording transaction. */
    ~FrameRecordingInput() = default;

    FrameRecordingInput(const FrameRecordingInput&) = delete;
    FrameRecordingInput& operator=(const FrameRecordingInput&) = delete;
    FrameRecordingInput(FrameRecordingInput&&) = delete;
    FrameRecordingInput& operator=(FrameRecordingInput&&) = delete;

    /** @brief Returns the shared values written once for both passes. @return Frame uniforms. */
    [[nodiscard]] const FrameUniforms& frameUniforms() const noexcept;
    /** @brief Returns the restricted shadow capability. @return Shadow state and caster packets. */
    [[nodiscard]] const ShadowRecordingInput& shadow() const noexcept;
    /** @brief Returns the restricted forward capability. @return Forward state and packets. */
    [[nodiscard]] const ForwardRecordingInput& forward() const noexcept;

private:
    friend class FrameRecordingInputCompiler;

    /**
     * @brief Freezes shared values and both restricted pass views.
     * @param frameUniforms Camera and light values shared by both passes.
     * @param shadowState Fixed shadow recording state.
     * @param shadowDraws Filtered caster packets in the compiler arena.
     * @param forwardState Fixed forward recording state.
     * @param forwardDraws Complete visible packets in the compiler arena.
     */
    FrameRecordingInput(FrameUniforms frameUniforms, ShadowRecordingState shadowState,
                        std::span<const ShadowRecordingDraw> shadowDraws,
                        ForwardRecordingState forwardState,
                        std::span<const ForwardRecordingDraw> forwardDraws) noexcept;

    FrameUniforms frameUniforms_;        ///< Shared slot-local values frozen once per frame.
    ShadowRecordingInput shadowInput_;   ///< Capability containing caster-only packets.
    ForwardRecordingInput forwardInput_; ///< Capability containing every visible packet.
};

/** @brief Resolves one scene snapshot into immutable shadow and forward transactions. */
class FrameRecordingInputCompiler final
{
public:
    /** @brief Creates empty reusable shadow and forward packet arenas. */
    FrameRecordingInputCompiler() = default;
    /** @brief Releases both arenas after the active frame input has expired. */
    ~FrameRecordingInputCompiler() = default;

    FrameRecordingInputCompiler(const FrameRecordingInputCompiler&) = delete;
    FrameRecordingInputCompiler& operator=(const FrameRecordingInputCompiler&) = delete;
    FrameRecordingInputCompiler(FrameRecordingInputCompiler&&) = delete;
    FrameRecordingInputCompiler& operator=(FrameRecordingInputCompiler&&) = delete;

    /**
     * @brief Resolves each external draw once and freezes both pass-specific packet sequences.
     * @param drawList External immutable scene snapshot consumed only during this call.
     * @param resources Restricted lookup into the current compiled-resource generation.
     * @param frameUniforms Shared camera and light values written after slot retirement.
     * @param shadowState Current frame-slot shadow recording state.
     * @param forwardState Current presentation and frame-slot forward recording state.
     * @return Immutable input valid until this compiler is used again.
     * @throws std::logic_error if pass requirements disagree or a draw was not prepared or is
     * pipeline-incompatible.
     * @pre The previous input returned by this compiler has no remaining CPU consumers.
     */
    [[nodiscard]] FrameRecordingInput compile(const SceneDrawList& drawList,
                                              CompiledResourcesView resources,
                                              FrameUniforms frameUniforms,
                                              ShadowRecordingState shadowState,
                                              ForwardRecordingState forwardState);

private:
    std::vector<ShadowRecordingDraw> shadowDraws_;   ///< Reused caster-only high-water arena.
    std::vector<ForwardRecordingDraw> forwardDraws_; ///< Reused all-draw high-water arena.
};
/** @endcond */
} // namespace detail
} // namespace fire_engine
