#pragma once

#include <array>

#include <fire_engine/render/detail/frame_slot.hpp>
#include <fire_engine/render/detail/frame_slot_count.hpp>
#include <fire_engine/render/detail/frame_uniform_buffer.hpp>
#include <fire_engine/render/detail/recording_context.hpp>
#include <fire_engine/render/detail/shadow_map.hpp>
#include <fire_engine/render/renderer.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Presentation-independent depth target and primary recording state for one shadow pass. */
struct ShadowFrameResources final
{
    ShadowMap map;            ///< Sampled depth target retired with the containing frame slot.
    RecordingContext primary; ///< Primary context reserved for the serial shadow pass.
};

/** @brief Primary and participant recording state owned by one forward pass. */
struct ForwardFrameResources final
{
    RecordingContext primary; ///< Primary-command recording state for the pass.
    // Both contexts exist in every configuration so one-thread and two-thread
    // measurements share an ownership topology. An allocated pool that is never
    // reset or recorded into contributes no measured work.
    std::array<RecordingContext, kMaxForwardRecordingParticipants>
        participants; ///< One secondary recording context per participant.
};

/** @brief Presentation-independent shared and pass-local state for one frame slot. */
struct FrameResources final
{
    FrameSlot slot;                ///< Synchronization state retiring the complete frame.
    FrameUniformBuffer uniforms;   ///< Shader values shared by both passes.
    ShadowFrameResources shadow;   ///< Depth target and dormant primary shadow context.
    ForwardFrameResources forward; ///< Existing forward primary and participant contexts.
};
/** @endcond */
} // namespace fire_engine::detail
