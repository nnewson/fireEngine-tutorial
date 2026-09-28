#pragma once

#include <vulkan/vulkan.hpp>

#include <fire_engine/render/detail/frame_recording_input.hpp>
#include <fire_engine/render/detail/recording_context.hpp>
#include <fire_engine/render/renderer.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Plain depth-attachment handles and compatibility values for one shadow pass. */
struct ShadowPassTarget final
{
    vk::Image depthImage;    ///< Selected frame-slot shadow image.
    vk::ImageView depthView; ///< Depth-only view of the selected image.
    vk::Format depthFormat;  ///< Sampled depth format compiled into the pipeline.
    vk::Extent2D extent;     ///< Complete shadow-map render extent.
};

/* --- Classes --- */

/** @brief Records one complete serial shadow primary without owning frame or GPU resources. */
class ShadowRecorder final
{
public:
    /**
     * @brief Resets the context and records depth writes followed by sampled-read publication.
     * @param primary Selected frame-slot shadow primary context.
     * @param input Immutable pipeline state and ordered caster packets.
     * @param target Plain selected shadow-map attachment handles.
     * @param timings Optional output receiving shadow durations and recorded draw count.
     * @pre The containing slot's fence has retired every prior use of the context and map.
     * @pre Shared frame uniforms have been written after that retirement.
     */
    void record(const RecordingContext& primary, const ShadowRecordingInput& input,
                const ShadowPassTarget& target, ShadowPassCpuTimings* timings) const;
};
/** @endcond */
} // namespace fire_engine::detail
