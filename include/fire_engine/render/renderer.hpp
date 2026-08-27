#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace fire_engine
{
/* --- Forward declarations --- */

class Glfw;
class RenderAssets;
class Scene;
class Window;

/* --- Enums --- */

/** @brief Command-buffer structure used to record one geometry pass. */
enum class CommandRecordingMode : std::uint8_t
{
    eSecondaryCommandBuffer, ///< Record draws in a secondary executed by the primary.
    eDirectPrimary,          ///< Record draws directly for attribution benchmarks.
};

/* --- POD structs --- */

/** @brief Construction-time renderer choices that remain fixed for its lifetime. */
struct RendererConfiguration
{
    CommandRecordingMode commandRecordingMode =
        CommandRecordingMode::eSecondaryCommandBuffer; ///< Geometry recording structure.
};

/** @brief Vulkan-free summary of the renderer selected for this window. */
struct RendererInfo
{
    std::string deviceName;                    ///< Vulkan-reported physical-device name.
    std::string driverName;                    ///< Vulkan-reported driver name.
    std::string driverInfo;                    ///< Driver-specific version and build information.
    std::uint32_t graphicsQueueFamily;         ///< Queue family used for graphics work.
    std::uint32_t presentQueueFamily;          ///< Queue family used for presentation.
    std::size_t swapchainImageCount;           ///< Number of presentable images.
    std::size_t presentationSemaphoreCount;    ///< One render-finished semaphore per image.
    std::uint32_t width;                       ///< Swapchain width in physical pixels.
    std::uint32_t height;                      ///< Swapchain height in physical pixels.
    std::string imageFormat;                   ///< Human-readable Vulkan image format.
    std::string depthFormat;                   ///< Human-readable depth attachment format.
    std::string presentMode;                   ///< Human-readable Vulkan presentation mode.
    CommandRecordingMode commandRecordingMode; ///< Geometry recording structure in use.
};

/** @brief Host timings for the CPU phases inside one drawFrame() attempt. */
struct RendererCpuTimings
{
    std::chrono::nanoseconds drawListBuild{};             ///< Snapshot allocation and traversal.
    std::chrono::nanoseconds drawListValidation{};        ///< Prepared-resource membership proof.
    std::chrono::nanoseconds frameFenceWait{};            ///< Reusable-frame completion wait.
    std::chrono::nanoseconds imageAcquisitionWait{};      ///< Presentable-image acquisition.
    std::chrono::nanoseconds presentationFenceWait{};     ///< Per-image retirement wait.
    std::chrono::nanoseconds commandPoolReset{};          ///< Serial reusable-pool reset.
    std::chrono::nanoseconds secondaryCommandRecording{}; ///< Worker-candidate draw recording.
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
 * prepare(), while drawFrame() consumes current scene transforms and records a
 * fresh command buffer. Presentation resources can be replaced independently,
 * preserving compiled scene resources across window changes.
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
     * @brief Validates and uploads the stable assets belonging to a scene.
     * @param assets Vulkan-free render descriptions to compile.
     * @param scene Hierarchy whose render-object references are validated.
     * Repeating the same asset revision and scene dependencies reuses the
     * cached plan and GPU resources. Changed inputs replace the compiled subset.
     *
     * @throws std::invalid_argument if the scene contains invalid data or references.
     * @throws std::runtime_error if a GPU allocation or upload fails.
     */
    void prepare(const RenderAssets& assets, const Scene& scene);

    /**
     * @brief Records current scene draws, submits them, and presents one image.
     * @param scene Prepared scene whose world transforms have been updated.
     * @param timings Optional output populated with host timings for this attempt.
     * @return Whether an image was presented and whether the swapchain remains suitable.
     * @throws std::logic_error if prepare() has not run or a scene reference was not prepared.
     * @throws vk::SystemError internally if an unexpected Vulkan operation fails.
     */
    [[nodiscard]] RenderResult drawFrame(const Scene& scene, RendererCpuTimings* timings = nullptr);

    /**
     * @brief Replaces presentation resources for the window's current framebuffer.
     * @param window Window whose framebuffer extent selects the replacement size.
     * @return true after replacement, or false when the framebuffer is currently zero-sized.
     * @throws std::runtime_error if the surface no longer supports presentation.
     * @throws vk::SystemError internally if Vulkan resource creation fails.
     *
     * A minimized window can become zero-sized between an event-loop check and
     * this call, so that transient state is reported rather than treated as an
     * error. Prepared meshes, textures, and render objects remain unchanged.
     */
    [[nodiscard]] bool recreatePresentation(const Window& window);

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
