#include <fire_engine/platform/window.hpp>

#include <stdexcept>
#include <string>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace fire_engine
{
/* --- Public member functions --- */

Window::Window(int width, int height, const std::string& title)
{
    if (width <= 0 || height <= 0)
    {
        throw std::invalid_argument("Window dimensions must be positive");
    }

    // Vulkan performs presentation itself, so GLFW must not create an OpenGL or
    // OpenGL ES context for this window.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // std::string supplies the null terminator GLFW requires. The pointer only
    // needs to remain valid for the synchronous glfwCreateWindow call.
    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (window_ == nullptr)
    {
        throw std::runtime_error("GLFW window creation failed");
    }
}

Window::~Window()
{
    glfwDestroyWindow(window_);
}

vk::raii::SurfaceKHR Window::createVulkanSurface(const vk::raii::Instance& instance) const
{
    // GLFW performs the platform-specific VkSurfaceKHR call. Wrapping the raw
    // result immediately gives the surface exception-safe RAII ownership.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result =
        glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window_, nullptr, &surface);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("GLFW Vulkan surface creation failed: " +
                                 vk::to_string(static_cast<vk::Result>(result)));
    }
    return {instance, surface};
}
} // namespace fire_engine
