#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <fire_engine/platform/framebuffer_extent.hpp>
#include <fire_engine/render/camera.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class Glfw;
class RenderAssets;
class Window;
struct SceneDrawList;

/* --- Enums --- */

/** @brief Command-buffer structure used to record one geometry pass. */
enum class CommandRecordingMode : std::uint8_t
{
    eSecondaryCommandBuffer, ///< Record draws in a secondary executed by the primary.
    eDirectPrimary,          ///< Record draws directly for attribution benchmarks.
};

/* --- Constants --- */

/** @brief Largest number of threads that may record secondary command buffers. */
inline constexpr std::size_t kMaxSecondaryRecordingThreads = 2;

/* --- POD structs --- */

/** @brief Construction-time renderer choices that remain fixed for its lifetime. */
struct RendererConfiguration
{
    CommandRecordingMode commandRecordingMode =
        CommandRecordingMode::eSecondaryCommandBuffer; ///< Geometry recording structure.
    /// Diagnostic override forcing a participant count, counting the coordinator itself.
    /// Unset selects the participant count from the workload, which is the production policy.
    std::optional<std::size_t> forcedSecondaryRecordingThreadCount;
};

/** @brief Vulkan-free summary of the renderer selected for this window. */
struct RendererInfo
{
    std::string deviceName;                    ///< Vulkan-reported physical-device name.
    std::string driverName;                    ///< Vulkan-reported driver name.
    std::string driverInfo;                    ///< Driver-specific version and build information.
    std::uint32_t graphicsQueueFamily;         ///< Queue family used for graphics work.
    std::uint32_t presentQueueFamily;          ///< Queue family used for presentation.
    std::size_t frameSlotCount;                ///< Submission slots cycled independently.
    std::size_t swapchainImageCount;           ///< Number of presentable images.
    std::size_t presentationSemaphoreCount;    ///< One render-finished semaphore per image.
    std::uint32_t width;                       ///< Swapchain width in physical pixels.
    std::uint32_t height;                      ///< Swapchain height in physical pixels.
    std::string imageFormat;                   ///< Human-readable Vulkan image format.
    std::string depthFormat;                   ///< Human-readable depth attachment format.
    std::string presentMode;                   ///< Human-readable Vulkan presentation mode.
    CommandRecordingMode commandRecordingMode; ///< Geometry recording structure in use.
    /// Diagnostic override in force, or unset when the workload selects the participant count.
    std::optional<std::size_t> forcedSecondaryRecordingThreadCount;
    /// Per-participant draw count at or above which the workload policy splits recording.
    std::size_t minimumDrawsPerRecordingParticipant;
};

/** @brief Pool-reset and recording durations for one secondary recording participant. */
struct ChunkCpuTimings
{
    std::chrono::nanoseconds poolReset{}; ///< This participant's own command-pool reset.
    std::chrono::nanoseconds recording{}; ///< This participant's secondary command recording.
    /// Delay from the earliest participant's reset start to this participant's own reset start.
    /// With the per-participant durations this reconstructs whether resets overlapped.
    std::chrono::nanoseconds resetStartOffset{};
    bool recorded = false; ///< Whether this participant ran during the attempt.
};

/** @brief Host timings for renderer-owned CPU phases inside one drawFrame() attempt. */
struct RendererCpuTimings
{
    std::chrono::nanoseconds recordingInputBuild{};         ///< Draw validation and packet freeze.
    std::chrono::nanoseconds frameUniformUpdate{};          ///< Slot-local per-frame value write.
    std::chrono::nanoseconds frameFenceWait{};              ///< Reusable-frame completion wait.
    std::chrono::nanoseconds imageAcquisitionWait{};        ///< Presentable-image acquisition.
    std::chrono::nanoseconds presentationFenceWait{};       ///< Per-image retirement wait.
    std::chrono::nanoseconds commandPoolReset{};            ///< Sum of pool resets on this path.
    std::chrono::nanoseconds coordinatorCommandPoolReset{}; ///< Primary-context pool reset.
    std::chrono::nanoseconds workerCommandPoolReset{};      ///< Worker-context pool reset.
    std::chrono::nanoseconds secondaryCommandRecording{};   ///< Sum of participant recording.
    /// Coordinator-observed span from publishing the helper chunk through its completion wait
    /// returning. This is the secondary-recording contribution to measured active work: unlike
    /// the participant sums it also contains dispatch and join, which threading adds.
    std::chrono::nanoseconds secondaryRecordingRegion{};
    /// Earliest participant reset start through the latest participant recording completion.
    /// Diagnostic input to the registered reset contract; never added to recording elapsed.
    std::chrono::nanoseconds workerRegionCriticalPath{};
    /// Earliest participant reset start through the latest participant reset end. Compare with
    /// the participant reset durations to decide whether concurrent resets overlapped.
    std::chrono::nanoseconds workerResetRegionSpan{};
    /// Entry to the coordinator's completion wait through its return. Contains any remaining
    /// helper execution as well as the wake.
    std::chrono::nanoseconds secondaryJoinWait{};
    /// Latest participant recording completion through the coordinator's wait returning. This is
    /// what the registered completion spin is intended to remove.
    std::chrono::nanoseconds secondaryCompletionTail{};
    /// Helper execution still outstanding when the coordinator entered its wait, defined
    /// independently of finish order as max(latestRecordEnd - joinStart, 0).
    std::chrono::nanoseconds secondaryHelperRemainingWork{};
    /// Whether polling observed completion within the registered budget.
    bool secondaryCompletionAcquiredBySpin = false;
    /// Whether the coordinator actually blocked. Not the complement of the flag above: completion
    /// can land after the last poll but before the fallback's first load, blocking neither way.
    bool secondaryCompletionUsedBlockingWait = false;
    std::array<ChunkCpuTimings, kMaxSecondaryRecordingThreads>
        chunks{};                                         ///< Per-participant detail.
    std::chrono::nanoseconds primaryCommandRecording{};   ///< Serial pass and transition recording.
    std::chrono::nanoseconds secondaryCommandExecution{}; ///< Serial secondary execution call.
    std::chrono::nanoseconds queueSubmission{};           ///< Fence reset and graphics submission.
    std::chrono::nanoseconds presentation{};              ///< Host presentation call.
};

/* --- Enums --- */

/** @brief Outcome of one attempt to render and present a frame. */
enum class RenderResult : std::uint8_t
{
    ePresented,           ///< Presented one image; the current swapchain remains suitable.
    ePresentedSuboptimal, ///< Presented one image, but the swapchain should be replaced.
    eNotPresented,        ///< Presented nothing because the swapchain is out of date.
};

/* --- Classes --- */

/**
 * @brief Owns Vulkan and compiles Vulkan-free scene descriptions for drawing.
 *
 * The public surface intentionally contains no Vulkan types. Stable mesh,
 * image, texture, and material descriptions are uploaded explicitly by
 * prepare(), while drawFrame() consumes a frozen scene draw-list view and
 * records a fresh command buffer. Presentation resources can be replaced
 * independently, preserving compiled scene resources across window changes.
 * Calls on one Renderer are serialized by the application; public member
 * functions must not be invoked concurrently.
 */
class Renderer final
{
public:
    /**
     * @brief Creates the complete Vulkan renderer for one window.
     * @param glfw Initialized GLFW lifetime owner.
     * @param window Window used to create and size the presentation surface.
     * @param applicationName Name reported to the Vulkan runtime.
     * @param configuration Fixed command-recording choices for this renderer.
     * @throws std::runtime_error if no suitable Vulkan configuration can be created.
     */
    Renderer(const Glfw& glfw, const Window& window, const std::string& applicationName,
             RendererConfiguration configuration = {});

    /** @brief Releases prepared resources and the Vulkan ownership tree. */
    ~Renderer() noexcept;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /**
     * @brief Validates and uploads stable assets required by a frozen draw list.
     * @param assets Vulkan-free render descriptions to compile.
     * @param drawList Frozen draws whose render-object references are validated.
     * Repeating the same asset revision and scene dependencies reuses the
     * cached plan and GPU resources. Changed inputs replace the compiled subset.
     *
     * @throws std::invalid_argument if assets or draw references are invalid.
     * @throws std::runtime_error if a GPU allocation or upload fails.
     */
    void prepare(const RenderAssets& assets, const SceneDrawList& drawList);

    /**
     * @brief Freezes frame input, records it, submits it, and presents one image.
     * @param drawList Read-only draw view valid until this call returns.
     * @param camera Vulkan-free camera values sampled for this frame.
     * @param timings Optional output populated with host timings for this attempt.
     * @return Whether an image was presented and whether the swapchain remains suitable.
     * @throws std::logic_error if prepare() has not run or a draw reference was not prepared.
     * @throws std::invalid_argument if the camera cannot form a valid perspective view.
     * @throws vk::SystemError internally if an unexpected Vulkan operation fails.
     */
    [[nodiscard]] RenderResult drawFrame(const SceneDrawList& drawList, const Camera& camera,
                                         RendererCpuTimings* timings = nullptr);

    /**
     * @brief Replaces presentation resources for a sampled framebuffer extent.
     * @param framebufferExtent Drawable size sampled by the application event loop.
     * @return true after replacement, or false when the framebuffer is currently zero-sized.
     * @throws std::runtime_error if the surface no longer supports presentation.
     * @throws vk::SystemError internally if Vulkan resource creation fails.
     *
     * A minimized window can become zero-sized between an event-loop check and
     * this call, so that transient state is reported rather than treated as an
     * error. Prepared meshes, textures, and render objects remain unchanged.
     */
    [[nodiscard]] bool recreatePresentation(FramebufferExtent framebufferExtent);

    /** @brief Waits for device and presentation work before resources are destroyed. */
    void waitIdle();

    /**
     * @brief Returns a Vulkan-free description of the initialized renderer.
     * @return Device, queues, swapchain, and presentation choices made at construction.
     */
    [[nodiscard]] RendererInfo info() const;

private:
    class Impl;
    std::unique_ptr<Impl> implementation_; ///< Hides Vulkan types and their lifetime ordering.
};
} // namespace fire_engine
