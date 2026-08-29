#include "benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <fire_engine/content/scene_content.hpp>
#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/math/transform.hpp>
#include <fire_engine/scene/scene.hpp>
#include <fire_engine/scene/scene_draw_list.hpp>
#include <fire_engine/scene/scene_node.hpp>

namespace fire_engine::tutorial
{
namespace
{
/** @cond INTERNAL */
/* --- File-local types --- */

/** @brief Aggregate statistics for one benchmark phase, expressed in microseconds. */
struct PhaseStatistics
{
    double meanMicroseconds = 0.0;   ///< Arithmetic mean across measured frames.
    double medianMicroseconds = 0.0; ///< Median across measured frames.
    double p95Microseconds = 0.0;    ///< Nearest-rank 95th percentile.
};

/* --- File-local function declarations --- */

/**
 * @brief Counts a subtree without consulting Scene's private dense registry.
 * @param node Root of the subtree to count.
 * @return Number of nodes reachable from node, including node itself.
 */
[[nodiscard]] std::size_t countSubtree(const SceneNode& node);

/**
 * @brief Counts every observable node owned by a scene.
 * @param scene Scene whose roots and descendants are traversed.
 * @return Number of nodes owned by the scene.
 */
[[nodiscard]] std::size_t countNodes(const Scene& scene);

/**
 * @brief Converts one duration to fractional microseconds.
 * @param duration Nanosecond duration to convert.
 * @return Equivalent fractional number of microseconds.
 */
[[nodiscard]] double microseconds(std::chrono::nanoseconds duration);

/**
 * @brief Names one command-buffer structure in benchmark output.
 * @param mode Recording mode selected when the renderer was constructed.
 * @return Stable human-readable label for comparisons between reports.
 */
[[nodiscard]] std::string_view recordingModeName(CommandRecordingMode mode);

/**
 * @brief Summarizes one non-empty collection of phase durations.
 * @param durations Durations to sort and aggregate.
 * @return Mean, median, and nearest-rank 95th percentile.
 * @throws std::logic_error if durations is empty.
 */
[[nodiscard]] PhaseStatistics summarize(std::vector<std::chrono::nanoseconds> durations);

/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Internal member functions --- */

BenchmarkRun::BenchmarkRun(SceneContent& content, std::size_t instanceCount)
    : instanceCount_{instanceCount}
{
    if (instanceCount_ == 0)
    {
        throw std::invalid_argument("The benchmark requires at least one cube instance");
    }
    if (content.assets.renderObjects().size() != 1)
    {
        throw std::logic_error("The AnimatedCube benchmark fixture must contain one render object");
    }

    const RenderObjectId object{.value = 0};
    auto root = std::make_unique<SceneNode>("Synthetic benchmark root");
    const std::size_t rowWidth =
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(instanceCount_))));
    const float center = static_cast<float>(rowWidth - 1) / 2.0f;
    constexpr float kSpacing = 2.5f;
    for (std::size_t index = 0; index < instanceCount_; ++index)
    {
        const std::size_t column = index % rowWidth;
        const std::size_t row = index / rowWidth;
        auto child = std::make_unique<SceneNode>();
        child->localTransform(Transform{
            .translation =
                {
                    .x = (static_cast<float>(column) - center) * kSpacing,
                    .y = (static_cast<float>(row) - center) * kSpacing,
                    .z = 0.0f,
                },
        });
        child->component(object);
        root->addChild(std::move(child));
    }

    Scene scene;
    SceneNode& registeredRoot = scene.addRoot(std::move(root));
    const std::optional<SceneNodeId> registeredRootId = registeredRoot.id();
    if (!registeredRootId.has_value())
    {
        throw std::logic_error("The synthetic benchmark root was not registered");
    }
    rootId_ = *registeredRootId;
    scene.updateWorldTransforms();
    SceneDrawListArena drawListArena;
    const SceneDrawList drawList = scene.buildDrawItems(drawListArena);
    nodeCount_ = countNodes(scene);
    drawCount_ = drawList.drawItems.size();
    if (nodeCount_ != instanceCount_ + 1 || drawCount_ != instanceCount_)
    {
        throw std::logic_error(
            "The synthetic benchmark hierarchy has unexpected node or draw counts");
    }

    content.scene = std::move(scene);
    content.animations.clear();
    samples_.reserve(kMeasuredFrameCount);
}

bool BenchmarkRun::complete() const noexcept
{
    return samples_.size() == kMeasuredFrameCount;
}

void BenchmarkRun::advanceScene(Scene& scene) const
{
    const std::optional<SceneNodeRef> root = scene.findNode(rootId_);
    if (!root.has_value())
    {
        throw std::logic_error("The synthetic benchmark root is no longer registered");
    }

    const std::uint64_t acceptedFrame = warmupFrames_ + static_cast<std::uint64_t>(samples_.size());
    Transform transform = root->get().localTransform();
    transform.translation.x =
        0.25f * std::sin(static_cast<float>(acceptedFrame) * kAnimationStepSeconds);
    root->get().localTransform(transform);
}

void BenchmarkRun::record(RenderResult result, std::chrono::nanoseconds transformUpdate,
                          std::chrono::nanoseconds drawListBuild,
                          const RendererCpuTimings& renderer)
{
    if (result != RenderResult::ePresented)
    {
        ++discardedAttempts_;
    }
    if (result == RenderResult::eNotPresented)
    {
        return;
    }
    if (warmupFrames_ < kWarmupFrameCount)
    {
        ++warmupFrames_;
        return;
    }
    if (result == RenderResult::ePresentedSuboptimal)
    {
        return;
    }
    samples_.push_back({
        .transformUpdate = transformUpdate,
        .drawListBuild = drawListBuild,
        .renderer = renderer,
    });
}

void BenchmarkRun::printReport(const RendererInfo& rendererInfo) const
{
    if (!complete())
    {
        throw std::logic_error("The benchmark report requires every measured frame");
    }

    // A configured split still falls back to one participant when the workload
    // is too small to form two non-empty ranges, so report what actually ran.
    std::size_t effectiveParticipants = 0;
    for (const ChunkCpuTimings& chunk : samples_.front().renderer.chunks)
    {
        if (chunk.recorded)
        {
            ++effectiveParticipants;
        }
    }
    for (const Sample& sample : samples_)
    {
        std::size_t sampleParticipants = 0;
        for (const ChunkCpuTimings& chunk : sample.renderer.chunks)
        {
            if (chunk.recorded)
            {
                ++sampleParticipants;
            }
        }
        if (sampleParticipants != effectiveParticipants)
        {
            throw std::logic_error("Measured frames disagree on the recording participant count");
        }
    }

    std::println("\nPhase-level CPU benchmark");
    std::println("  Build configuration: {}", FIRE_ENGINE_BUILD_CONFIGURATION);
    std::println("  Device: {}", rendererInfo.deviceName);
    std::println("  Driver: {} ({})", rendererInfo.driverName, rendererInfo.driverInfo);
    std::println("  Recording path: {}", recordingModeName(rendererInfo.commandRecordingMode));
    if (rendererInfo.commandRecordingMode == CommandRecordingMode::eSecondaryCommandBuffer)
    {
        std::println("  Secondary recording participants: {} configured, {} effective",
                     rendererInfo.secondaryRecordingThreadCount, effectiveParticipants);
    }
    std::println("  Ownership: cycled frame slots with per-slot recording contexts (Step 5)");
    std::println("  Frames in flight: {}", rendererInfo.frameSlotCount);
    std::println("  Presentation: {}x{}, {}, {}", rendererInfo.width, rendererInfo.height,
                 rendererInfo.imageFormat, rendererInfo.presentMode);
    std::println("  Workload: instances={}, nodes={}, draws={}", instanceCount_, nodeCount_,
                 drawCount_);
    std::println("  Frames: {} warm-up, {} measured, {} discarded attempts", kWarmupFrameCount,
                 kMeasuredFrameCount, discardedAttempts_);
    std::println("  Fixed animation step: {:.6f} seconds", kAnimationStepSeconds);
    std::println("\n  {:<36} {:>12} {:>12} {:>12}", "Phase", "mean us", "median us", "p95 us");

    const auto printPhase = [this](std::string_view name, const auto& projection)
    {
        std::vector<std::chrono::nanoseconds> durations;
        durations.reserve(samples_.size());
        for (const Sample& sample : samples_)
        {
            durations.push_back(std::invoke(projection, sample));
        }
        const PhaseStatistics statistics = summarize(std::move(durations));
        std::println("  {:<36} {:>12.3f} {:>12.3f} {:>12.3f}", name, statistics.meanMicroseconds,
                     statistics.medianMicroseconds, statistics.p95Microseconds);
    };

    printPhase("transform update", &Sample::transformUpdate);
    printPhase("draw-list build", &Sample::drawListBuild);
    printPhase("recording-input build",
               [](const Sample& sample) { return sample.renderer.recordingInputBuild; });
    printPhase("frame-uniform update",
               [](const Sample& sample) { return sample.renderer.frameUniformUpdate; });
    printPhase("coordinator command-pool reset",
               [](const Sample& sample) { return sample.renderer.coordinatorCommandPoolReset; });
    // With more than one participant these are summed participant CPU time, not
    // an elapsed phase: the participants overlap, so the sums are diagnostics
    // rather than a contribution to active work.
    printPhase(effectiveParticipants > 1 ? "worker pool reset (summed CPU)"
                                         : "worker command-pool reset",
               [](const Sample& sample) { return sample.renderer.workerCommandPoolReset; });
    printPhase(effectiveParticipants > 1 ? "secondary recording (summed CPU)"
                                         : "secondary command recording",
               [](const Sample& sample) { return sample.renderer.secondaryCommandRecording; });
    printPhase("secondary recording region",
               [](const Sample& sample) { return sample.renderer.secondaryRecordingRegion; });
    if (effectiveParticipants > 1)
    {
        printPhase("worker-region critical path",
                   [](const Sample& sample) { return sample.renderer.workerRegionCriticalPath; });
        printPhase("worker reset-region span",
                   [](const Sample& sample) { return sample.renderer.workerResetRegionSpan; });
        for (std::size_t participant = 0; participant < effectiveParticipants; ++participant)
        {
            printPhase(std::format("participant {} pool reset", participant),
                       [participant](const Sample& sample)
                       { return sample.renderer.chunks[participant].poolReset; });
            printPhase(std::format("participant {} recording", participant),
                       [participant](const Sample& sample)
                       { return sample.renderer.chunks[participant].recording; });
            printPhase(std::format("participant {} reset start offset", participant),
                       [participant](const Sample& sample)
                       { return sample.renderer.chunks[participant].resetStartOffset; });
        }
    }
    printPhase("primary command recording",
               [](const Sample& sample) { return sample.renderer.primaryCommandRecording; });
    printPhase("secondary command execution",
               [](const Sample& sample) { return sample.renderer.secondaryCommandExecution; });
    printPhase("queue submission",
               [](const Sample& sample) { return sample.renderer.queueSubmission; });
    printPhase("frame-fence wait",
               [](const Sample& sample) { return sample.renderer.frameFenceWait; });
    printPhase("image-acquisition wait",
               [](const Sample& sample) { return sample.renderer.imageAcquisitionWait; });
    printPhase("presentation-fence wait",
               [](const Sample& sample) { return sample.renderer.presentationFenceWait; });
    printPhase("presentation call",
               [](const Sample& sample) { return sample.renderer.presentation; });

    std::chrono::nanoseconds snapshot{};
    std::chrono::nanoseconds workerCommandPoolReset{};
    std::chrono::nanoseconds secondaryRecording{};
    std::chrono::nanoseconds secondaryRecordingRegion{};
    std::chrono::nanoseconds primaryRecording{};
    std::chrono::nanoseconds secondaryExecution{};
    std::chrono::nanoseconds submission{};
    std::chrono::nanoseconds activeWork{};
    for (const Sample& sample : samples_)
    {
        const std::chrono::nanoseconds sampleSnapshot =
            sample.transformUpdate + sample.drawListBuild + sample.renderer.recordingInputBuild;
        snapshot += sampleSnapshot;
        workerCommandPoolReset += sample.renderer.workerCommandPoolReset;
        secondaryRecording += sample.renderer.secondaryCommandRecording;
        secondaryRecordingRegion += sample.renderer.secondaryRecordingRegion;
        primaryRecording += sample.renderer.primaryCommandRecording;
        secondaryExecution += sample.renderer.secondaryCommandExecution;
        submission += sample.renderer.queueSubmission;
        // The secondary-recording region, not the participant sums, is what
        // enters active work. With more than one participant the sums overlap
        // in time and exclude dispatch and join, so adding them would both
        // double-count parallel work and hide the cost threading introduces.
        activeWork += sampleSnapshot + sample.renderer.coordinatorCommandPoolReset +
                      sample.renderer.frameUniformUpdate +
                      sample.renderer.secondaryRecordingRegion +
                      sample.renderer.primaryCommandRecording +
                      sample.renderer.secondaryCommandExecution + sample.renderer.queueSubmission;
    }
    if (activeWork.count() == 0)
    {
        throw std::logic_error("The benchmark recorded no active CPU work");
    }
    const double activeCount = static_cast<double>(activeWork.count());
    const auto percentageOfActive = [activeCount](std::chrono::nanoseconds duration)
    { return 100.0 * static_cast<double>(duration.count()) / activeCount; };

    std::println("\n  Snapshot share of measured active work: {:.2f}%",
                 percentageOfActive(snapshot));
    std::println("  Queue-submission share of measured active work: {:.2f}%",
                 percentageOfActive(submission));
    std::println("  Fence, acquisition, and presentation durations are reported separately.");
    if (rendererInfo.commandRecordingMode == CommandRecordingMode::eSecondaryCommandBuffer)
    {
        // Worker-owned work is the region the coordinator observes, which is
        // the quantity the registered model divides.
        const std::chrono::nanoseconds attributedWorkerWork = secondaryRecordingRegion;
        const std::chrono::nanoseconds serialOutsideWorker = activeWork - attributedWorkerWork;
        std::println("  Secondary-execution share of measured active work: {:.2f}%",
                     percentageOfActive(secondaryExecution));
        std::println("  Secondary-recording region share of measured active work: {:.2f}%",
                     percentageOfActive(attributedWorkerWork));
        std::println("  Current serial share outside that region: {:.2f}%",
                     percentageOfActive(serialOutsideWorker));
        if (effectiveParticipants > 1)
        {
            // This configuration measures a realized split rather than predicting
            // one, so the single-participant projection does not apply. Summed
            // participant CPU time is not a share of elapsed active work.
            std::println("  Summed participant durations overlap in time and are reported as "
                         "diagnostics only.");
            std::println("  Compare the reset-region span with the participant reset durations to "
                         "judge overlap.");
            std::println("  The recording region includes dispatch and join, so it prices the "
                         "threading overhead.");
        }
        else
        {
            std::println("  Worker-pool-reset share of measured active work: {:.2f}%",
                         percentageOfActive(workerCommandPoolReset));
            std::println(
                "  Split phases attribute ownership; they do not measure placement benefit.");
            std::println(
                "  Registered p subtracts the one-draw fixed reset estimate from worker work.");
            std::println("  For two workers: serial + fixed reset + variable worker work / 2.");
        }
    }
    else
    {
        std::println(
            "  The direct-primary control has no worker-divisible secondary-recording phase.");
        std::println("  Its worker pool has no command-buffer allocations or recorded work.");
    }
    std::println("  Draw bindings are cached independently inside each recorded command buffer.");
}

/** @endcond */

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

[[nodiscard]] std::size_t countSubtree(const SceneNode& node)
{
    std::size_t count = 1;
    for (const std::unique_ptr<SceneNode>& child : node.children())
    {
        count += countSubtree(*child);
    }
    return count;
}

[[nodiscard]] std::size_t countNodes(const Scene& scene)
{
    std::size_t count = 0;
    for (const std::unique_ptr<SceneNode>& root : scene.roots())
    {
        count += countSubtree(*root);
    }
    return count;
}

[[nodiscard]] double microseconds(std::chrono::nanoseconds duration)
{
    return std::chrono::duration<double, std::micro>{duration}.count();
}

[[nodiscard]] std::string_view recordingModeName(CommandRecordingMode mode)
{
    switch (mode)
    {
    case CommandRecordingMode::eSecondaryCommandBuffer:
        return "secondary command buffer";
    case CommandRecordingMode::eDirectPrimary:
        return "direct primary command buffer";
    }
    throw std::logic_error("Benchmark encountered an unknown command recording mode");
}

[[nodiscard]] PhaseStatistics summarize(std::vector<std::chrono::nanoseconds> durations)
{
    if (durations.empty())
    {
        throw std::logic_error("Benchmark phase statistics require at least one sample");
    }
    std::ranges::sort(durations);

    std::chrono::nanoseconds total{};
    for (const std::chrono::nanoseconds duration : durations)
    {
        total += duration;
    }

    const std::size_t middle = durations.size() / 2;
    const double median =
        durations.size() % 2 == 0
            ? (microseconds(durations[middle - 1]) + microseconds(durations[middle])) / 2.0
            : microseconds(durations[middle]);
    const std::size_t p95Index = (durations.size() * 95 + 99) / 100 - 1;
    return {
        .meanMicroseconds = microseconds(total) / static_cast<double>(durations.size()),
        .medianMicroseconds = median,
        .p95Microseconds = microseconds(durations[p95Index]),
    };
}
/** @endcond */
} // namespace

} // namespace fire_engine::tutorial
