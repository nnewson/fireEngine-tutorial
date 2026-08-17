/**
 * @file
 * @brief Program entry point, tutorial scene construction, and platform event loop.
 */

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <fire_engine/animation/animation_playback.hpp>
#include <fire_engine/content/scene_content.hpp>
#include <fire_engine/core/log.hpp>
#include <fire_engine/gltf/gltf_loader.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/renderer.hpp>
#include <fire_engine/scene/scene.hpp>

namespace
{
/** @cond INTERNAL */
/* --- File-local types --- */

/** @brief Command-line controls used by automated integration runs. */
struct RunOptions
{
    std::optional<std::uint64_t> frameLimit; ///< Presented frames requested before exit.
    bool recreateEveryFrame = false; ///< Whether every presented frame replaces presentation state.
};

/* --- File-local function declarations --- */

/**
 * @brief Reads the optional frame limit and presentation-recreation smoke mode.
 * @param argumentCount Number of command-line arguments including the executable.
 * @param arguments Null-terminated argument strings supplied by the host environment.
 * @return Parsed controls, or interactive defaults when no arguments are supplied.
 * @throws std::invalid_argument if the command line does not match the documented usage.
 */
[[nodiscard]] RunOptions parseOptions(int argumentCount, char* arguments[]);

/**
 * @brief Waits without spinning and retries recreation until the framebuffer is drawable.
 * @param renderer Renderer whose presentation-dependent state is replaced.
 * @param window Window whose events and framebuffer extent are inspected.
 * @return False when closure was requested before recreation succeeded.
 */
[[nodiscard]] bool recreateWhenDrawable(fire_engine::Renderer& renderer,
                                        const fire_engine::Window& window);

/** @endcond */
} // namespace

/* --- Public functions --- */

/**
 * @brief Runs the Vulkan application and owns its platform event loop.
 * @param argumentCount Number of command-line arguments including the executable.
 * @param arguments Null-terminated argument strings supplied by the host environment.
 * @return Zero after a clean shutdown; otherwise one.
 */
int main(int argumentCount, char* arguments[])
try
{
    const RunOptions options = parseOptions(argumentCount, arguments);
    const std::string applicationName = "fireEngine Tutorial";

    fire_engine::Glfw glfw;
    fire_engine::Window window{800, 600, applicationName};
    fire_engine::Renderer renderer{glfw, window, applicationName};
    fire_engine::SceneContent content = fire_engine::GltfLoader{}.load(
        std::filesystem::path{FIRE_ENGINE_ASSET_DIRECTORY} / "AnimatedCube" / "AnimatedCube.gltf");

    content.scene.updateWorldTransforms();
    renderer.prepare(content.assets, content.scene);

    const fire_engine::RendererInfo rendererInfo = renderer.info();
    std::println("Selected Vulkan 1.4 device: {}", rendererInfo.deviceName);
    std::println("Graphics queue family: {}", rendererInfo.graphicsQueueFamily);
    std::println("Present queue family: {}", rendererInfo.presentQueueFamily);
    std::println("Logical device, queues, and VMA allocator created.");
    std::println("Swapchain created: {} images at {}x{} ({}, {}, depth {}), {} presentation "
                 "semaphores.",
                 rendererInfo.swapchainImageCount, rendererInfo.width, rendererInfo.height,
                 rendererInfo.imageFormat, rendererInfo.presentMode, rendererInfo.depthFormat,
                 rendererInfo.presentationSemaphoreCount);
    std::println("AnimatedCube prepared: one indexed mesh and sampled base-color texture.");

    std::uint64_t renderedFrameCount = 0;
    auto previousFrameTime = std::chrono::steady_clock::now();
    while (!window.shouldClose() &&
           (!options.frameLimit.has_value() || renderedFrameCount < *options.frameLimit))
    {
        window.pollEvents();
        if (window.shouldClose())
        {
            break;
        }

        const auto currentFrameTime = std::chrono::steady_clock::now();
        const float elapsedSeconds =
            std::chrono::duration<float>{currentFrameTime - previousFrameTime}.count();
        previousFrameTime = currentFrameTime;
        fire_engine::advanceAnimations(content.scene, content.animations, elapsedSeconds);
        content.scene.updateWorldTransforms();

        if (window.consumeFramebufferResize())
        {
            if (!recreateWhenDrawable(renderer, window))
            {
                break;
            }
        }

        const fire_engine::RenderResult result = renderer.drawFrame(content.scene);
        if (result != fire_engine::RenderResult::eNotPresented)
        {
            ++renderedFrameCount;
        }
        if (result != fire_engine::RenderResult::ePresented || options.recreateEveryFrame)
        {
            // Coalesce a resize callback with the out-of-date or suboptimal
            // result that the same surface change may have produced.
            static_cast<void>(window.consumeFramebufferResize());
            if (!recreateWhenDrawable(renderer, window))
            {
                break;
            }
        }
    }

    // Presentation is not covered by the per-frame fence. Renderer::waitIdle
    // first waits for device work, then waits for every presentation fence
    // supplied through the KHR or equivalent EXT swapchain-maintenance extension.
    // Together they make submitted and presentation resources safe to destroy.
    renderer.waitIdle();

    if (options.frameLimit.has_value() && renderedFrameCount != *options.frameLimit)
    {
        throw std::runtime_error("The smoke test ended before presenting every requested frame");
    }
    std::println("Presented {} frame{}.", renderedFrameCount, renderedFrameCount == 1 ? "" : "s");
    return 0;
}
catch (const std::exception& error)
{
    fire_engine::log("fireEngine Tutorial failed: {}", error.what());
    return 1;
}

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

[[nodiscard]] RunOptions parseOptions(int argumentCount, char* arguments[])
{
    if (argumentCount == 1)
    {
        return {};
    }
    if ((argumentCount != 3 && argumentCount != 4) ||
        std::string_view{arguments[1]} != "--frames" ||
        (argumentCount == 4 && std::string_view{arguments[3]} != "--recreate-every-frame"))
    {
        throw std::invalid_argument(
            "Usage: fireEngineTutorial [--frames positive-count [--recreate-every-frame]]");
    }

    const std::string_view valueText{arguments[2]};
    std::uint64_t value = 0;
    const auto [end, error] =
        std::from_chars(valueText.data(), valueText.data() + valueText.size(), value);
    if (error != std::errc{} || end != valueText.data() + valueText.size() || value == 0)
    {
        throw std::invalid_argument("--frames requires a positive integer");
    }
    return {
        .frameLimit = value,
        .recreateEveryFrame = argumentCount == 4,
    };
}

[[nodiscard]] bool recreateWhenDrawable(fire_engine::Renderer& renderer,
                                        const fire_engine::Window& window)
{
    while (!window.shouldClose())
    {
        const vk::Extent2D extent = window.framebufferExtent();
        if (extent.width != 0 && extent.height != 0 && renderer.recreatePresentation(window))
        {
            return true;
        }
        window.waitEvents();
    }
    return false;
}

/** @endcond */
} // namespace
