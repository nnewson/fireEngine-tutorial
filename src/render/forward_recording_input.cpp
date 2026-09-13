#include <fire_engine/render/detail/forward_recording_input.hpp>

#include <fire_engine/scene/scene_draw_list.hpp>

#include <optional>
#include <stdexcept>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

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

ForwardRecordingInput ForwardRecordingInputCompiler::compile(const SceneDrawList& drawList,
                                                             CompiledResourcesView resources,
                                                             ForwardRecordingState state)
{
    draws_.clear();
    for (const DrawItem& item : drawList.drawItems)
    {
        const std::optional<CompiledRenderObject> compiledObject =
            resources.find(item.renderObject);
        if (!compiledObject.has_value())
        {
            throw std::logic_error("Scene refers to an object not compiled by prepare");
        }
        const CompiledRenderObject& object = compiledObject.value();
        if (object.geometry.vertexLayout != state.vertexLayout)
        {
            throw std::logic_error(
                "Compiled geometry is incompatible with the forward recording pipeline");
        }
        draws_.push_back({
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
    }
    return ForwardRecordingInput{state, draws_};
}
/** @endcond */
} // namespace fire_engine::detail
