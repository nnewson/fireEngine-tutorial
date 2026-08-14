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
/* --- File-local function declarations --- */

/**
 * @brief Reads the optional frame limit used by the automated smoke test.
 * @param argumentCount Number of command-line arguments including the executable.
 * @param arguments Null-terminated argument strings supplied by the host environment.
 * @return Requested positive frame count, or no limit for an interactive run.
 * @throws std::invalid_argument if the command line is not `--frames N`.
 */
[[nodiscard]] std::optional<std::uint64_t> parseFrameLimit(int argumentCount, char* arguments[]);

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
    const std::optional<std::uint64_t> frameLimit = parseFrameLimit(argumentCount, arguments);
    const std::string applicationName = "fireEngine Tutorial";

    fire_engine::Glfw glfw;
    const fire_engine::Window window{800, 600, applicationName};
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
    bool swapchainNeedsRecreation = false;
    auto previousFrameTime = std::chrono::steady_clock::now();
    while (!window.shouldClose() && (!frameLimit.has_value() || renderedFrameCount < *frameLimit))
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
        const fire_engine::RenderResult result = renderer.drawFrame(content.scene);
        if (result != fire_engine::RenderResult::eNotPresented)
        {
            ++renderedFrameCount;
        }
        if (result != fire_engine::RenderResult::ePresented)
        {
            swapchainNeedsRecreation = true;
            break;
        }
    }

    // Presentation is not covered by the per-frame fence. Waiting for the whole
    // device covers the submitted work the VMA buffers depend on. For presentation
    // resources it is the conventional shutdown fallback rather than a
    // specification guarantee; deferred destruction or presentation fences are
    // the specification-backed solutions once recreation exists.
    renderer.waitIdle();

    if (swapchainNeedsRecreation)
    {
        std::println("The surface changed; swapchain recreation is left to a later tutorial.");
    }
    if (frameLimit.has_value() && renderedFrameCount != *frameLimit)
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

[[nodiscard]] std::optional<std::uint64_t> parseFrameLimit(int argumentCount, char* arguments[])
{
    if (argumentCount == 1)
    {
        return std::nullopt;
    }
    if (argumentCount != 3 || std::string_view{arguments[1]} != "--frames")
    {
        throw std::invalid_argument("Usage: fireEngineTutorial [--frames positive-count]");
    }

    const std::string_view valueText{arguments[2]};
    std::uint64_t value = 0;
    const auto [end, error] =
        std::from_chars(valueText.data(), valueText.data() + valueText.size(), value);
    if (error != std::errc{} || end != valueText.data() + valueText.size() || value == 0)
    {
        throw std::invalid_argument("--frames requires a positive integer");
    }
    return value;
}

/** @endcond */
} // namespace
