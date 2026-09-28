#include "fire_engine/render/detail/forward_secondary_recording_worker.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace
{
using fire_engine::detail::ChunkRecordingTimings;
using fire_engine::detail::ForwardSecondaryChunkJob;
using fire_engine::detail::ForwardSecondaryRecordingWorker;

std::atomic<int> gCompletedChunks{0};

// Written by the helper and read only after awaitCompletion(): its acquire
// observes the helper's publication, just as it does for participant timings.
vk::ImageView gObservedShadowMapView;
vk::Sampler gObservedShadowComparisonSampler;

template <typename Handle, typename NativeHandle>
[[nodiscard]] Handle fakeHandle(std::uintptr_t value)
{
    if constexpr (std::is_pointer_v<NativeHandle>)
    {
        return Handle{reinterpret_cast<NativeHandle>(value)}; // NOLINT(performance-no-int-to-ptr)
    }
    else
    {
        return Handle{static_cast<NativeHandle>(value)};
    }
}

/** @brief Observes copied fixed sampling handles without making a Vulkan call. */
void samplingStateRecorder(const ForwardSecondaryChunkJob& job, ChunkRecordingTimings*)
{
    gObservedShadowMapView = job.state.shadowMapView;
    gObservedShadowComparisonSampler = job.state.shadowComparisonSampler;
}

/** @brief Records nothing but proves the helper invoked the recorder. */
void countingRecorder(const ForwardSecondaryChunkJob&, ChunkRecordingTimings* timings)
{
    gCompletedChunks.fetch_add(1, std::memory_order_relaxed);
    if (timings != nullptr)
    {
        timings->recorded = true;
    }
}

/** @brief Fails the way a Vulkan call inside a chunk would. */
void throwingRecorder(const ForwardSecondaryChunkJob&, ChunkRecordingTimings*)
{
    throw std::runtime_error("chunk failure");
}
} // namespace

static_assert(!std::is_copy_constructible_v<ForwardSecondaryRecordingWorker>);
static_assert(!std::is_move_constructible_v<ForwardSecondaryRecordingWorker>);
static_assert(std::is_trivially_copyable_v<ForwardSecondaryChunkJob>);

TEST_CASE("Forward secondary recording worker runs a dispatched chunk and reports idleness")
{
    gCompletedChunks.store(0, std::memory_order_relaxed);
    ForwardSecondaryRecordingWorker worker;
    REQUIRE(worker.idle());

    ChunkRecordingTimings timings;
    const ForwardSecondaryChunkJob job;
    worker.dispatch(&countingRecorder, job, &timings);
    worker.awaitCompletion();

    REQUIRE(worker.idle());
    REQUIRE(gCompletedChunks.load(std::memory_order_relaxed) == 1);
    REQUIRE(timings.recorded);
    REQUIRE_NOTHROW(worker.rethrowIfFailed());
}

TEST_CASE("Forward secondary recording worker preserves copied shadow sampling handles")
{
    gObservedShadowMapView = nullptr;
    gObservedShadowComparisonSampler = nullptr;
    ForwardSecondaryRecordingWorker worker;

    const auto shadowMapView = fakeHandle<vk::ImageView, VkImageView>(30);
    const auto shadowComparisonSampler = fakeHandle<vk::Sampler, VkSampler>(31);
    ForwardSecondaryChunkJob job;
    job.state.shadowMapView = shadowMapView;
    job.state.shadowComparisonSampler = shadowComparisonSampler;
    worker.dispatch(&samplingStateRecorder, job, nullptr);
    // dispatch() copies the complete job before publication. Changing the
    // caller's values must not change the handles seen by the helper.
    job.state.shadowMapView = nullptr;
    job.state.shadowComparisonSampler = nullptr;
    worker.awaitCompletion();
    REQUIRE_NOTHROW(worker.rethrowIfFailed());

    REQUIRE(gObservedShadowMapView == shadowMapView);
    REQUIRE(gObservedShadowComparisonSampler == shadowComparisonSampler);
}

TEST_CASE("Forward secondary recording worker reports ordered completion-wait boundaries")
{
    ForwardSecondaryRecordingWorker worker;

    const ForwardSecondaryChunkJob job;
    worker.dispatch(&countingRecorder, job, nullptr);
    worker.awaitCompletion();

    // Which outcome occurred depends on scheduling and is not asserted. The
    // interval must be well formed, and the two outcome flags are mutually
    // exclusive because observing completion while polling skips the fallback.
    const auto& wait = worker.lastCompletionWait();
    REQUIRE(wait.start <= wait.end);
    REQUIRE(wait.start != std::chrono::steady_clock::time_point{});
    REQUIRE_FALSE((wait.acquiredBySpin && wait.usedBlockingWait));
}

TEST_CASE("Forward secondary recording worker omits instrumentation when no block is supplied")
{
    gCompletedChunks.store(0, std::memory_order_relaxed);
    ForwardSecondaryRecordingWorker worker;

    const ForwardSecondaryChunkJob job;
    worker.dispatch(&countingRecorder, job, nullptr);
    worker.awaitCompletion();

    REQUIRE(gCompletedChunks.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("Forward secondary recording worker reports a chunk failure only after completion")
{
    ForwardSecondaryRecordingWorker worker;

    const ForwardSecondaryChunkJob job;
    worker.dispatch(&throwingRecorder, job, nullptr);
    // Completion is signalled even though the chunk threw, so the coordinator
    // can always wait before its job arguments expire.
    REQUIRE_NOTHROW(worker.awaitCompletion());
    REQUIRE(worker.idle());
    REQUIRE_THROWS_AS(worker.rethrowIfFailed(), std::runtime_error);
}

TEST_CASE("Forward secondary recording worker clears a previous failure before the next chunk")
{
    ForwardSecondaryRecordingWorker worker;

    const ForwardSecondaryChunkJob job;
    worker.dispatch(&throwingRecorder, job, nullptr);
    worker.awaitCompletion();

    // Deliberately leave that failure unobserved. rethrowIfFailed() would clear
    // it itself, so calling it here would prove nothing about dispatch().
    gCompletedChunks.store(0, std::memory_order_relaxed);
    worker.dispatch(&countingRecorder, job, nullptr);
    worker.awaitCompletion();

    REQUIRE(gCompletedChunks.load(std::memory_order_relaxed) == 1);
    REQUIRE_NOTHROW(worker.rethrowIfFailed());
}

TEST_CASE("Forward secondary recording worker rethrows a failure that was never observed")
{
    ForwardSecondaryRecordingWorker worker;

    const ForwardSecondaryChunkJob job;
    worker.dispatch(&throwingRecorder, job, nullptr);
    worker.awaitCompletion();
    // Dropping the failure without rethrowing must not leak into the next
    // chunk, which the clearing test above covers, and must not prevent a
    // clean destruction here.
    REQUIRE(worker.idle());
}
