#pragma once

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

/* --- POD structs --- */

/** @brief Vulkan-free summary of the renderer selected for this window. */
struct RendererInfo
{
    std::string deviceName;                 ///< Vulkan-reported physical-device name.
    std::uint32_t graphicsQueueFamily;      ///< Queue family used for graphics work.
    std::uint32_t presentQueueFamily;       ///< Queue family used for presentation.
    std::size_t swapchainImageCount;        ///< Number of presentable images.
    std::size_t presentationSemaphoreCount; ///< One render-finished semaphore per image.
    std::uint32_t width;                    ///< Swapchain width in physical pixels.
    std::uint32_t height;                   ///< Swapchain height in physical pixels.
    std::string imageFormat;                ///< Human-readable Vulkan image format.
    std::string depthFormat;                ///< Human-readable depth attachment format.
    std::string presentMode;                ///< Human-readable Vulkan presentation mode.
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
 * The public surface intentionally contains no Vulkan types. Stable mesh and
 * material descriptions are uploaded explicitly by prepare(), while drawFrame()
 * consumes current scene transforms and records a fresh command buffer.
 */
class Renderer final
{
public:
    /**
     * @brief Creates the complete Vulkan renderer for one window.
     * @param glfw Initialized GLFW lifetime owner.
     * @param window Window used to create and size the presentation surface.
     * @param applicationName Name reported to the Vulkan runtime.
     * @throws std::runtime_error if no suitable Vulkan configuration can be created.
     */
    Renderer(const Glfw& glfw, const Window& window, const std::string& applicationName);

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
     * @return Whether an image was presented and whether the swapchain remains suitable.
     * @throws std::logic_error if prepare() has not run or a scene reference was not prepared.
     * @throws vk::SystemError internally if an unexpected Vulkan operation fails.
     */
    [[nodiscard]] RenderResult drawFrame(const Scene& scene);

    /** @brief Waits for device work before renderer-dependent resources are destroyed. */
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
