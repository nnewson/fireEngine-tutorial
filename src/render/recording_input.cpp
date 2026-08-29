#include <fire_engine/render/detail/recording_input.hpp>

#include <fire_engine/scene/scene_draw_list.hpp>

#include <optional>
#include <stdexcept>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

RecordingInput::RecordingInput(RecordingState state, std::span<const RecordingDraw> draws) noexcept
    : state_{state},
      draws_{draws}
{
}

const RecordingState& RecordingInput::state() const noexcept
{
    return state_;
}

std::span<const RecordingDraw> RecordingInput::draws() const noexcept
{
    return draws_;
}

RecordingInput RecordingInputCompiler::compile(const SceneDrawList& drawList,
                                               CompiledResourcesView resources,
                                               RecordingState state)
{
    draws_.clear();
    for (const DrawItem& item : drawList.drawItems)
    {
        const std::optional<CompiledDraw> compiledDraw = resources.find(item.renderObject);
        if (!compiledDraw.has_value())
        {
            throw std::logic_error("Scene refers to an object not compiled by prepare");
        }
        const CompiledDraw& draw = compiledDraw.value();
        if (draw.vertexLayout != state.vertexLayout)
        {
            throw std::logic_error("Compiled draw is incompatible with the recording pipeline");
        }
        draws_.push_back({
            .vertexBuffer = draw.vertexBuffer,
            .indexBuffer = draw.indexBuffer,
            .indexCount = draw.indexCount,
            .sampler = draw.sampler,
            .imageView = draw.imageView,
            .constants =
                {
                    .model = item.world,
                    .baseColor = draw.baseColor,
                },
        });
    }
    return RecordingInput{state, draws_};
}
/** @endcond */
} // namespace fire_engine::detail
