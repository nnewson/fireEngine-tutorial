/**
 * @file
 * @brief Program entry point and event loop for the triangle renderer.
 */

#include <charconv>
#include <cstdint>
#include <exception>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fire_engine/core/log.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/allocator.hpp>
#include <fire_engine/render/device.hpp>
#include <fire_engine/render/pipeline.hpp>
#include <fire_engine/render/renderer.hpp>
#include <fire_engine/render/swapchain.hpp>

namespace
{
/* --- File-local functions --- */

/**
 * @brief Reads the optional frame limit used by the automated smoke test.
 * @param argumentCount Number of command-line arguments including the executable.
 * @param arguments Null-terminated argument strings supplied by the host environment.
 * @return Requested positive frame count, or no limit for an interactive run.
 * @throws std::invalid_argument if the command line is not `--frames N`.
 */
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

    // Local declaration order mirrors the complete ownership chain: GLFW
    // outlives the window, Device owns the surface, and each later render
    // resource is released before the Vulkan objects on which it depends.
    fire_engine::Glfw glfw;
    const fire_engine::Window window{800, 600, applicationName};
    const fire_engine::Device device{glfw, window, applicationName};
    const fire_engine::MemoryAllocator allocator{device};
    const fire_engine::Swapchain swapchain{device, window};
    const fire_engine::Pipeline pipeline{device, swapchain.imageFormat()};
    fire_engine::Renderer renderer{device, allocator, swapchain, pipeline};

    // Vulkan guarantees a valid handle for a queue family and index that were
    // already validated during logical-device creation, so this cannot fail in
    // practice. It is here so the smoke test exercises the accessors rather than
    // printing a claim about the queues that nothing checks.
    if (!*device.graphicsQueue() || !*device.presentQueue())
    {
        throw std::runtime_error("Vulkan returned a null device queue");
    }
    if (allocator.handle() == nullptr)
    {
        throw std::runtime_error("VMA returned a null allocator");
    }
    if (swapchain.imageCount() == 0 || swapchain.imageViews().size() != swapchain.images().size() ||
        swapchain.renderFinished().size() != swapchain.imageCount())
    {
        throw std::runtime_error("Vulkan returned an incomplete swapchain");
    }

    std::println("Selected Vulkan 1.4 device: {}", device.name());
    std::println("Graphics queue family: {}", device.graphicsQueueFamily());
    std::println("Present queue family: {}", device.presentQueueFamily());
    std::println("Logical device and queues created.");
    std::println("VMA allocator created.");
    std::println("Swapchain created: {} images at {}x{} ({}, {}), {} presentation semaphores.",
                 swapchain.imageCount(), swapchain.extent().width, swapchain.extent().height,
                 vk::to_string(swapchain.imageFormat()), vk::to_string(swapchain.presentMode()),
                 swapchain.renderFinished().size());
    std::println("Pipeline layout and dynamic-rendering pipeline created.");
    std::println("Triangle buffers and one frame in flight created.");

    std::uint64_t renderedFrameCount = 0;
    bool swapchainNeedsRecreation = false;
    while (!window.shouldClose() && (!frameLimit.has_value() || renderedFrameCount < *frameLimit))
    {
        window.pollEvents();
        if (window.shouldClose())
        {
            break;
        }

        const fire_engine::RenderResult result = renderer.renderFrame();
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
    // The logger catches formatting failures and falls back to allocation-free
    // C stdio, preserving the original application error.
    fire_engine::log("fireEngine Tutorial failed: {}", error.what());
    return 1;
}
