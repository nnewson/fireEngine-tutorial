/**
 * @file
 * @brief Program entry point for the milestone 2 startup smoke test.
 */

#include <exception>
#include <print>
#include <stdexcept>
#include <string>

#include <fire_engine/core/log.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/device.hpp>

/* --- Public functions --- */

/**
 * @brief Runs the milestone 2 Vulkan startup smoke test.
 * @return Zero after successful device and queue creation; otherwise one.
 */
int main()
try
{
    const std::string applicationName = "fireEngine Tutorial";

    // GLFW must outlive the window, and the window must outlive the Vulkan
    // surface owned by Device. Local declaration order gives us that teardown.
    fire_engine::Glfw glfw;
    const fire_engine::Window window{800, 600, applicationName};
    const fire_engine::Device device{glfw, window, applicationName};

    // Vulkan guarantees a valid handle for a queue family and index that were
    // already validated during logical-device creation, so this cannot fail in
    // practice. It is here so the smoke test exercises the accessors rather than
    // printing a claim about the queues that nothing checks.
    if (!*device.graphicsQueue() || !*device.presentQueue())
    {
        throw std::runtime_error("Vulkan returned a null device queue");
    }

    std::println("Selected Vulkan 1.4 device: {}", device.name());
    std::println("Graphics queue family: {}", device.graphicsQueueFamily());
    std::println("Present queue family: {}", device.presentQueueFamily());
    std::println("Logical device and queues created.");
    return 0;
}
catch (const std::exception& error)
{
    // The logger catches formatting failures and falls back to allocation-free
    // C stdio, preserving the original startup error.
    fire_engine::log("Vulkan startup failed: {}", error.what());
    return 1;
}
