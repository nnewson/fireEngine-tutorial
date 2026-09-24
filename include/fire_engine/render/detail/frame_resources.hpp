#pragma once

#include <array>

#include <fire_engine/render/detail/forward_frame_uniform_buffer.hpp>
#include <fire_engine/render/detail/frame_slot.hpp>
#include <fire_engine/render/detail/frame_slot_count.hpp>
#include <fire_engine/render/detail/recording_context.hpp>
#include <fire_engine/render/detail/shadow_map.hpp>
#include <fire_engine/render/renderer.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Presentation-independent synchronization, uniform, and recording state for one slot. */
struct FrameResources final
{
    FrameSlot slot;                            ///< Synchronization state for the slot.
    ForwardFrameUniformBuffer forwardUniforms; ///< Forward values for the slot.
    ShadowMap shadowMap;                       ///< Presentation-independent sampled depth target.
    RecordingContext coordinator;              ///< Primary-command recording state for the slot.
    // Both contexts exist in every configuration so one-thread and two-thread
    // measurements share an ownership topology. An allocated pool that is never
    // reset or recorded into contributes no measured work.
    std::array<RecordingContext, kMaxForwardRecordingParticipants>
        secondaries; ///< One recording context per participant.
};
/** @endcond */
} // namespace fire_engine::detail
