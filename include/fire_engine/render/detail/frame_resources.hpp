#pragma once

#include <array>

#include <fire_engine/render/detail/frame_slot.hpp>
#include <fire_engine/render/detail/frame_slot_count.hpp>
#include <fire_engine/render/detail/recording_context.hpp>
#include <fire_engine/render/renderer.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Presentation-independent submission and recording state for one frame slot. */
struct FrameResources final
{
    FrameSlot slot;               ///< Synchronization and uniform state for the slot.
    RecordingContext coordinator; ///< Primary-command recording state for the slot.
    // Both contexts exist in every configuration so one-thread and two-thread
    // measurements share an ownership topology. An allocated pool that is never
    // reset or recorded into contributes no measured work.
    std::array<RecordingContext, kMaxSecondaryRecordingThreads>
        secondaries; ///< One recording context per participant.
};
/** @endcond */
} // namespace fire_engine::detail
