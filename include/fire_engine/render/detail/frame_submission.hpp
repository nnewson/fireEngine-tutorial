#pragma once

#include <vulkan/vulkan.hpp>

#include <fire_engine/render/detail/frame_barriers.hpp>

#include <array>
#include <cstdint>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Constants --- */

/**
 * @brief Stage scope of the timeline signal that retires each frame submission.
 *
 * The signal certifies completion of all submitted command work: both pass
 * primaries and, for capture, the readback transfer, which lies outside the
 * graphics stages that the render-finished signal covers. It does not cover the
 * render-finished signal in the same batch or presentation.
 */
inline constexpr vk::PipelineStageFlags2 kSubmissionRetirementStage =
    vk::PipelineStageFlagBits2::eAllCommands;

/* --- POD structs --- */

/** @brief Owned signal operations for one frame submission. */
struct FrameSignals
{
    std::array<vk::SemaphoreSubmitInfo, 2> infos{}; ///< Operations; the first count are used.
    std::uint32_t count = 0;                        ///< Number of signal operations to submit.
};

/* --- Functions --- */

/**
 * @brief Builds the semaphore signals for one frame submission.
 *
 * Every submission signals its serial on the timeline. An ordinary frame also
 * signals the acquired image's render-finished semaphore, which presentation
 * waits on. Capture signals no binary semaphore: it retires the submission on
 * the host before presenting, so a render-finished signal would never be
 * consumed and would remain signaled when the image is reused.
 *
 * @param timeline Renderer-owned timeline semaphore.
 * @param serial Serial this submission signals on the timeline.
 * @param renderFinished Acquired image's render-finished semaphore.
 * @param capture Whether this submission carries the capture readback.
 * @return Owned signal entries: the timeline first, followed by render-finished for
 *         ordinary frames. Array order does not order their execution; signals in
 *         one submission are unordered relative to each other.
 */
[[nodiscard]] constexpr FrameSignals frameSignalSemaphoreInfos(vk::Semaphore timeline,
                                                               std::uint64_t serial,
                                                               vk::Semaphore renderFinished,
                                                               bool capture) noexcept
{
    FrameSignals signals{};
    signals.infos[0] = {
        .semaphore = timeline,
        .value = serial,
        .stageMask = kSubmissionRetirementStage,
    };
    signals.count = 1;
    if (!capture)
    {
        signals.infos[1] = {
            .semaphore = renderFinished,
            .stageMask = kRenderFinishedSignalStage,
        };
        signals.count = 2;
    }
    return signals;
}
/** @endcond */
} // namespace fire_engine::detail
