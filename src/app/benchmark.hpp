#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <fire_engine/render/renderer.hpp>
#include <fire_engine/scene/scene_node_id.hpp>

namespace fire_engine
{
class Scene;
struct SceneContent;

namespace tutorial
{
/** @cond INTERNAL */

/** @brief Owns one deterministic benchmark workload and its measured CPU samples. */
class BenchmarkRun final
{
public:
    /**
     * @brief Replaces loaded content with repeated instances of its sole render object.
     * @param content AnimatedCube content supplying one reusable compiled cube.
     * @param instanceCount Positive number of synthetic cube instances to create.
     * @throws std::invalid_argument if instanceCount is zero.
     * @throws std::logic_error if the fixture does not contain exactly one render object.
     */
    BenchmarkRun(SceneContent& content, std::size_t instanceCount);

    /**
     * @brief Reports whether every warm-up and measured frame has completed.
     * @return true once the fixed measured sample count has been collected.
     */
    [[nodiscard]] bool complete() const noexcept;

    /**
     * @brief Applies the fixed-step deterministic mutation for the next accepted frame.
     * @param scene Synthetic hierarchy whose shared root transform is changed.
     */
    void advanceScene(Scene& scene) const;

    /**
     * @brief Accepts or discards one attempted frame's timings.
     * @param result Presentation result for this attempt.
     * @param transformUpdate Time spent resolving world transforms.
     * @param drawListBuild Time spent replacing the arena-backed draw-list snapshot.
     * @param renderer Timings measured inside Renderer::drawFrame().
     */
    void record(RenderResult result, std::chrono::nanoseconds transformUpdate,
                std::chrono::nanoseconds drawListBuild, const RendererCpuTimings& renderer);

    /**
     * @brief Prints aggregate phase measurements after the run completes.
     * @param rendererInfo Device, driver, and presentation metadata for this run.
     * @throws std::logic_error if called before every measured sample exists.
     */
    void printReport(const RendererInfo& rendererInfo) const;

private:
    /** @brief Complete timing sample for one cleanly presented measured frame. */
    struct Sample
    {
        std::chrono::nanoseconds transformUpdate{}; ///< Serial world-transform resolution.
        std::chrono::nanoseconds drawListBuild{};   ///< Serial arena-backed snapshot build.
        RendererCpuTimings renderer{};              ///< Renderer-owned host phases.
    };

    static constexpr std::uint64_t kWarmupFrameCount = 16;   ///< Frames discarded before sampling.
    static constexpr std::uint64_t kMeasuredFrameCount = 64; ///< Clean samples retained per run.
    static constexpr float kAnimationStepSeconds = 1.0f / 60.0f; ///< Deterministic mutation step.

    std::size_t instanceCount_ = 0;       ///< Repeated uses of the one compiled cube.
    std::size_t nodeCount_ = 0;           ///< Observable nodes in the synthetic hierarchy.
    std::size_t drawCount_ = 0;           ///< Observable draw items in the synthetic hierarchy.
    SceneNodeId rootId_;                  ///< Shared root mutated before transform resolution.
    std::uint64_t warmupFrames_ = 0;      ///< Presented warm-up frames already discarded.
    std::uint64_t discardedAttempts_ = 0; ///< Out-of-date or suboptimal frame attempts.
    std::vector<Sample> samples_;         ///< Fixed-count samples retained for aggregation.
};

/** @endcond */
} // namespace tutorial
} // namespace fire_engine
