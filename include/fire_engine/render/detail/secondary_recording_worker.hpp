#pragma once

#include <atomic>
#include <chrono>
#include <exception>
#include <semaphore>
#include <span>
#include <thread>

#include <fire_engine/render/detail/recording_input.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class RecordingContext;

/* --- Constants --- */

/**
 * @brief Measured bound on active polling inside the completion wait.
 *
 * Active polling stops at the first deadline check at or after this budget.
 * Preemption can make the wall time exceed it without the loop performing any
 * further polling while descheduled.
 */
inline constexpr std::chrono::nanoseconds kCompletionSpinBudget{std::chrono::microseconds{50}};

/* --- POD structs --- */

#if defined(_MSC_VER)
// Separating participants onto their own cache lines is the entire purpose of
// the alignment specifier below, so the padding MSVC reports as C4324 is
// intentional rather than accidental.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

/**
 * @brief Timestamps captured by one recording participant.
 *
 * Each participant owns a separate cache line so concurrent writes cannot
 * share one. Timestamps are compared across threads only after both
 * participants finish; steady_clock maps to a single monotonic system clock on
 * every supported target, so those comparisons are meaningful.
 */
struct alignas(64) ChunkRecordingTimings
{
    std::chrono::steady_clock::time_point resetStart{}; ///< Before this chunk's pool reset.
    std::chrono::steady_clock::time_point resetEnd{};   ///< After this chunk's pool reset.
    std::chrono::steady_clock::time_point recordEnd{};  ///< After this chunk's secondary ends.
    bool recorded = false;                              ///< Whether this chunk ran this frame.
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/** @brief Everything one participant needs to record its contiguous draw range. */
struct SecondaryChunkJob
{
    const RecordingContext* context = nullptr; ///< Pool and secondary buffer owned by the chunk.
    RecordingState state{};                    ///< Fixed state copied so no caller frame is read.
    std::span<const RecordingDraw> draws;      ///< Contiguous packets recorded by this chunk.
};

/**
 * @brief Outcome of one coordinator completion wait.
 *
 * The two flags are not complements. Completion can be published after the
 * final polling load but before the fallback loop's first load, in which case
 * neither polling observed it nor did the coordinator ever block. Reporting
 * both separates that boundary case from a genuine block.
 */
struct CompletionWait
{
    std::chrono::steady_clock::time_point start{}; ///< Entry to the wait.
    std::chrono::steady_clock::time_point end{};   ///< After completion was observed.
    bool acquiredBySpin = false;                   ///< Polling observed completion.
    bool usedBlockingWait = false;                 ///< The coordinator actually blocked.
};

/**
 * @brief Records one chunk into its own secondary command buffer.
 *
 * The timing block is null on ordinary frames so production recording does not
 * pay for instrumentation.
 */
using SecondaryChunkRecorder = void (*)(const SecondaryChunkJob&, ChunkRecordingTimings*);

/* --- Classes --- */

/**
 * @brief One persistent thread that records a single secondary chunk on request.
 *
 * This is deliberately not a scheduler or a general thread pool. The
 * coordinator records the first chunk itself and hands exactly one chunk to
 * this helper, so the fixed cost per frame is one request and one completion.
 *
 * The helper never receives renderer ownership. Its job carries one recording
 * context, a copy of the fixed recording state, and a span of already resolved
 * packets, so it cannot reach presentation state, queues, the allocator, or a
 * resource owner.
 *
 * No helper state survives one frame. dispatch() must be followed by
 * awaitCompletion() before the caller's job arguments expire, which the
 * renderer guarantees with a scope guard so the wait also happens while an
 * exception unwinds.
 */
class SecondaryRecordingWorker final
{
public:
    /** @brief Starts the helper thread parked on its request semaphore. */
    SecondaryRecordingWorker();

    /** @brief Requests shutdown and joins the helper. */
    ~SecondaryRecordingWorker() noexcept;

    /// @brief Copy construction is disabled because the helper owns a thread.
    SecondaryRecordingWorker(const SecondaryRecordingWorker&) = delete;
    /// @brief Copy assignment is disabled because the helper owns a thread.
    SecondaryRecordingWorker& operator=(const SecondaryRecordingWorker&) = delete;
    /// @brief Move construction is disabled so the running thread's owner is fixed.
    SecondaryRecordingWorker(SecondaryRecordingWorker&&) = delete;
    /// @brief Move assignment is disabled so the running thread's owner is fixed.
    SecondaryRecordingWorker& operator=(SecondaryRecordingWorker&&) = delete;

    /**
     * @brief Publishes one chunk and wakes the helper.
     * @param recorder Function the helper invokes with the published job.
     * @param job Chunk description, copied here. Its referenced recording context and draw
     * storage must remain valid until the matching awaitCompletion() returns.
     * @param timings Participant-local block the helper timestamps, or null to skip
     * instrumentation. It must outlive the matching awaitCompletion().
     * @pre The helper is idle.
     */
    void dispatch(SecondaryChunkRecorder recorder, const SecondaryChunkJob& job,
                  ChunkRecordingTimings* timings) noexcept;

    /**
     * @brief Blocks until the dispatched chunk has stopped reading its job.
     *
     * Polls the completion flag for at most kCompletionSpinBudget, then blocks
     * on that same flag. The flag rather than a semaphore is the completion
     * primitive: a semaphore released before the flag was published would let
     * this call return, the next dispatch clear the flag, and the helper then
     * publish a stale completion that the following wait would misreport as a
     * successful spin. Because the coordinator cannot proceed until the store
     * is visible, no publication can be outstanding when the flag is cleared.
     *
     * Safe to call while an exception unwinds; it neither throws nor reports
     * the helper's own failure. Call rethrowIfFailed() afterwards.
     */
    void awaitCompletion() noexcept;

    /**
     * @brief Returns how the most recent completion wait ended.
     * @return Wait boundaries and whether polling observed completion.
     * @pre awaitCompletion() has returned for that dispatch.
     */
    [[nodiscard]] const CompletionWait& lastCompletionWait() const noexcept;

    /**
     * @brief Rethrows a failure captured by the helper's most recent chunk.
     * @throws Whatever the helper's recorder threw.
     * @pre awaitCompletion() has returned for that dispatch.
     */
    void rethrowIfFailed();

    /**
     * @brief Reports whether the helper holds no outstanding chunk.
     * @return True when every dispatch has been matched by an awaitCompletion().
     */
    [[nodiscard]] bool idle() const noexcept;

private:
    /** @brief Parks on the request semaphore and records one chunk per wake-up. */
    void run() noexcept;

    // Written by the coordinator before releasing request_ and read by the
    // helper after acquiring it, so the semaphore supplies the ordering.
    SecondaryChunkRecorder recorder_ = nullptr; ///< Recording function for the current chunk.
    SecondaryChunkJob job_;                     ///< Current chunk description.
    ChunkRecordingTimings* timings_ = nullptr;  ///< Optional participant block for the chunk.
    std::exception_ptr failure_;                ///< Failure captured by the current chunk.
    std::atomic<bool> stopping_{false};         ///< Set once, before the final request.
    // Cleared by dispatch and published by the helper. Its acquire observation
    // supplies the happens-before for the participant timings and any captured
    // failure, so no separate completion semaphore is needed.
    std::atomic<bool> completionPublished_{false}; ///< Completion signal and wait primitive.
    CompletionWait lastCompletionWait_{};          ///< Outcome of the most recent wait.
    bool outstanding_ = false;                     ///< Coordinator-side dispatch bookkeeping.
    std::binary_semaphore request_{0};             ///< Released by dispatch, acquired by run.
    std::thread thread_;                           ///< Joined by the destructor.
};
/** @endcond */
} // namespace fire_engine::detail
