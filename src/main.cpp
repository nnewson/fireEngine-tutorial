/**
 * @file
 * @brief Program entry point for the version 0.5 startup smoke test.
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
#include <fire_engine/render/frame_in_flight.hpp>
#include <fire_engine/render/pipeline.hpp>
#include <fire_engine/render/swapchain.hpp>

/* --- Public functions --- */

/**
 * @brief Runs the version 0.5 Vulkan startup smoke test.
 * @return Zero after successful render-resource creation; otherwise one.
 */
int main()
try
{
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
    const fire_engine::FrameInFlight frame{device};

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

    // Individual handles are not checked. Every vk::raii constructor throws on
    // failure, so reaching this line already proves each handle these objects
    // own is valid. What is worth checking is what construction does not prove:
    // that the pieces relate to each other as intended, as the per-image
    // semaphore count above does, and that requested state actually took
    // effect, as the initially signaled fence does below.
    if (frame.frameFinished().getStatus() != vk::Result::eSuccess)
    {
        throw std::runtime_error("The frame-finished fence was not initially signaled");
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
    std::println("Primary command buffer and one-frame synchronization created.");
    return 0;
}
catch (const std::exception& error)
{
    // The logger catches formatting failures and falls back to allocation-free
    // C stdio, preserving the original startup error.
    fire_engine::log("Vulkan startup failed: {}", error.what());
    return 1;
}
