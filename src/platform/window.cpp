#include <fire_engine/platform/window.hpp>

#include <cstdint>
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
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
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

FramebufferExtent Window::framebufferExtent() const noexcept
{
    // Framebuffer pixels, rather than logical window coordinates, match the
    // resolution required by Vulkan on high-DPI displays.
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {
        .width = width > 0 ? static_cast<std::uint32_t>(width) : 0U,
        .height = height > 0 ? static_cast<std::uint32_t>(height) : 0U,
    };
}

bool Window::shouldClose() const noexcept
{
    return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void Window::pollEvents() const noexcept
{
    // GLFW owns the process-wide event queue, but keeping this call on Window
    // prevents its C API and native handle from leaking into the application loop.
    glfwPollEvents();
}

void Window::waitEvents() const noexcept
{
    // Unlike polling, this blocks while a minimized window has no drawable
    // framebuffer, avoiding a busy loop until restoration generates an event.
    glfwWaitEvents();
}

bool Window::consumeFramebufferResize() noexcept
{
    const bool result = framebufferResized_;
    framebufferResized_ = false;
    return result;
}

/* --- Private static functions --- */

void Window::framebufferSizeCallback(GLFWwindow* window, int, int) noexcept
{
    // Width and height are queried again when recreating because several resize
    // callbacks may be coalesced before the event loop handles this notification.
    auto* const owner = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (owner != nullptr)
    {
        owner->framebufferResized_ = true;
    }
}
} // namespace fire_engine
