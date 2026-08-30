#include <fire_engine/render/detail/secondary_recording_worker.hpp>

#include <cassert>
#include <utility>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

SecondaryRecordingWorker::SecondaryRecordingWorker()
    : thread_{&SecondaryRecordingWorker::run, this}
{
}

SecondaryRecordingWorker::~SecondaryRecordingWorker() noexcept
{
    // Destruction with a chunk still outstanding would mean a dispatch escaped
    // its scope guard. The frame loop cannot reach here in that state.
    assert(idle());
    stopping_.store(true, std::memory_order_relaxed);
    request_.release();
    thread_.join();
}

void SecondaryRecordingWorker::dispatch(SecondaryChunkRecorder recorder,
                                        const SecondaryChunkJob& job,
                                        ChunkRecordingTimings* timings) noexcept
{
    assert(idle());
    // Clear the previous failure so an earlier chunk cannot be reported twice.
    failure_ = nullptr;
    completionPublished_.store(false, std::memory_order_relaxed);
    recorder_ = recorder;
    job_ = job;
    timings_ = timings;
    outstanding_ = true;
    request_.release();
}

void SecondaryRecordingWorker::awaitCompletion() noexcept
{
    if (!outstanding_)
    {
        lastCompletionWait_ = {};
        return;
    }

    // The clock reads below are part of the mechanism rather than
    // instrumentation, so they are taken on every frame and are priced inside
    // the coordinator-observed recording region.
    const auto start = std::chrono::steady_clock::now();
    bool acquiredBySpin = false;
    while (true)
    {
        if (completionPublished_.load(std::memory_order_acquire))
        {
            acquiredBySpin = true;
            break;
        }
        // Checked after every unsuccessful poll: batching the check would let
        // the loop run past the budget for an unbounded machine-dependent span.
        if (std::chrono::steady_clock::now() - start >= kCompletionSpinBudget)
        {
            break;
        }
    }

    // Blocking fallback on the same flag. wait() may return spuriously, so the
    // load is what decides. This loop can also exit without blocking when
    // completion lands between the final polling load and the first load here.
    bool usedBlockingWait = false;
    while (!completionPublished_.load(std::memory_order_acquire))
    {
        usedBlockingWait = true;
        completionPublished_.wait(false, std::memory_order_acquire);
    }
    lastCompletionWait_ = {
        .start = start,
        .end = std::chrono::steady_clock::now(),
        .acquiredBySpin = acquiredBySpin,
        .usedBlockingWait = usedBlockingWait,
    };
    outstanding_ = false;
}

const CompletionWait& SecondaryRecordingWorker::lastCompletionWait() const noexcept
{
    return lastCompletionWait_;
}

void SecondaryRecordingWorker::rethrowIfFailed()
{
    assert(idle());
    if (failure_)
    {
        std::rethrow_exception(std::exchange(failure_, nullptr));
    }
}

bool SecondaryRecordingWorker::idle() const noexcept
{
    return !outstanding_;
}

void SecondaryRecordingWorker::run() noexcept
{
    while (true)
    {
        request_.acquire();
        if (stopping_.load(std::memory_order_relaxed))
        {
            return;
        }

        // Every exception is captured rather than escaping the thread, both
        // because an uncaught one would terminate the process and because the
        // coordinator must still observe completion before its job expires.
        try
        {
            recorder_(job_, timings_);
        }
        catch (...)
        {
            failure_ = std::current_exception();
        }
        // Publication is the completion signal itself. Releasing a separate
        // semaphore first would let the coordinator return, clear the flag for
        // the next chunk, and only then see this store land as a stale
        // completion.
        completionPublished_.store(true, std::memory_order_release);
        completionPublished_.notify_one();
    }
}
/** @endcond */
} // namespace fire_engine::detail
