/**
 * @file
 * @brief Program entry point for the milestone 0.4 startup smoke test.
 */

#include <exception>
#include <print>
#include <stdexcept>
#include <string>

#include <fire_engine/core/log.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/allocator.hpp>
#include <fire_engine/render/device.hpp>
#include <fire_engine/render/pipeline.hpp>
#include <fire_engine/render/swapchain.hpp>

/* --- Public functions --- */

/**
 * @brief Runs the milestone 0.4 Vulkan startup smoke test.
 * @return Zero after successful allocator, swapchain, and pipeline creation; otherwise one.
 */
int main()
try
{
    const std::string applicationName = "fireEngine Tutorial";

    // Local declaration order mirrors the complete ownership chain: GLFW
    // outlives the window, Device owns the surface, and the allocator,
    // swapchain, and pipeline are released before their Vulkan dependencies.
    fire_engine::Glfw glfw;
    const fire_engine::Window window{800, 600, applicationName};
    const fire_engine::Device device{glfw, window, applicationName};
    const fire_engine::MemoryAllocator allocator{device};
    const fire_engine::Swapchain swapchain{device, window};
    const fire_engine::Pipeline pipeline{device, swapchain.imageFormat()};

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
    if (swapchain.imageCount() == 0 || swapchain.imageViews().size() != swapchain.images().size())
    {
        throw std::runtime_error("Vulkan returned an incomplete swapchain");
    }

    // Pipeline is not checked here. Its vk::raii members throw on failure, so
    // reaching this line already proves every handle it owns is valid.

    std::println("Selected Vulkan 1.4 device: {}", device.name());
    std::println("Graphics queue family: {}", device.graphicsQueueFamily());
    std::println("Present queue family: {}", device.presentQueueFamily());
    std::println("Logical device and queues created.");
    std::println("VMA allocator created.");
    std::println("Swapchain created: {} images at {}x{} ({}, {}).", swapchain.imageCount(),
                 swapchain.extent().width, swapchain.extent().height,
                 vk::to_string(swapchain.imageFormat()), vk::to_string(swapchain.presentMode()));
    std::println("Pipeline layout and dynamic-rendering pipeline created.");
    return 0;
}
catch (const std::exception& error)
{
    // The logger catches formatting failures and falls back to allocation-free
    // C stdio, preserving the original startup error.
    fire_engine::log("Vulkan startup failed: {}", error.what());
    return 1;
}
