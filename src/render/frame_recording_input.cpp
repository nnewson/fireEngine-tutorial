#include <fire_engine/render/detail/frame_recording_input.hpp>

#include <fire_engine/scene/scene_draw_list.hpp>

#include <optional>
#include <stdexcept>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

ShadowRecordingInput::ShadowRecordingInput(ShadowRecordingState state,
                                           std::span<const ShadowRecordingDraw> draws) noexcept
    : state_{state},
      draws_{draws}
{
}

const ShadowRecordingState& ShadowRecordingInput::state() const noexcept
{
    return state_;
}

std::span<const ShadowRecordingDraw> ShadowRecordingInput::draws() const noexcept
{
    return draws_;
}

ForwardRecordingInput::ForwardRecordingInput(ForwardRecordingState state,
                                             std::span<const ForwardRecordingDraw> draws) noexcept
    : state_{state},
      draws_{draws}
{
}

const ForwardRecordingState& ForwardRecordingInput::state() const noexcept
{
    return state_;
}

std::span<const ForwardRecordingDraw> ForwardRecordingInput::draws() const noexcept
{
    return draws_;
}

FrameRecordingInput::FrameRecordingInput(
    FrameUniforms frameUniforms, ShadowRecordingState shadowState,
    std::span<const ShadowRecordingDraw> shadowDraws, ForwardRecordingState forwardState,
    std::span<const ForwardRecordingDraw> forwardDraws) noexcept
    : frameUniforms_{frameUniforms},
      shadowInput_{shadowState, shadowDraws},
      forwardInput_{forwardState, forwardDraws}
{
}

const FrameUniforms& FrameRecordingInput::frameUniforms() const noexcept
{
    return frameUniforms_;
}

const ShadowRecordingInput& FrameRecordingInput::shadow() const noexcept
{
    return shadowInput_;
}

const ForwardRecordingInput& FrameRecordingInput::forward() const noexcept
{
    return forwardInput_;
}

FrameRecordingInput FrameRecordingInputCompiler::compile(const SceneDrawList& drawList,
                                                         CompiledResourcesView resources,
                                                         FrameUniforms frameUniforms,
                                                         ShadowRecordingState shadowState,
                                                         ForwardRecordingState forwardState)
{
    if (shadowState.vertexLayout != forwardState.vertexLayout)
    {
        throw std::logic_error(
            "Shadow and forward recording pipelines use different vertex layouts");
    }

    shadowDraws_.clear();
    forwardDraws_.clear();
    for (const DrawItem& item : drawList.drawItems)
    {
        const std::optional<CompiledRenderObject> compiledObject =
            resources.find(item.renderObject);
        if (!compiledObject.has_value())
        {
            throw std::logic_error("Scene refers to an object not compiled by prepare");
        }
        const CompiledRenderObject& object = compiledObject.value();
        if (object.geometry.vertexLayout != forwardState.vertexLayout)
        {
            throw std::logic_error(
                "Compiled geometry is incompatible with the frame recording pipelines");
        }

        forwardDraws_.push_back({
            .vertexBuffer = object.geometry.vertexBuffer,
            .indexBuffer = object.geometry.indexBuffer,
            .indexCount = object.geometry.indexCount,
            .sampler = object.forwardMaterial.sampler,
            .imageView = object.forwardMaterial.imageView,
            .constants =
                {
                    .model = item.world,
                    .baseColor = object.forwardMaterial.baseColor,
                },
        });
        if (object.castsShadow)
        {
            shadowDraws_.push_back({
                .vertexBuffer = object.geometry.vertexBuffer,
                .indexBuffer = object.geometry.indexBuffer,
                .indexCount = object.geometry.indexCount,
                .constants = {.model = item.world},
            });
        }
    }

    return FrameRecordingInput{frameUniforms, shadowState, shadowDraws_, forwardState,
                               forwardDraws_};
}
/** @endcond */
} // namespace fire_engine::detail
