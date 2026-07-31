#pragma once

#include <string>

#include <vulkan/vulkan_raii.hpp>

/* --- External forward declarations --- */

struct GLFWwindow;

namespace fire_engine
{
/* --- Classes --- */

// Owns the native window while hiding GLFW's platform-specific surface work
// from the renderer.
class Window final
{
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    // The returned RAII surface is owned by Vulkan and must be destroyed before
    // its instance; it does not own this Window.
    [[nodiscard]] vk::raii::SurfaceKHR
    createVulkanSurface(const vk::raii::Instance& instance) const;

private:
    GLFWwindow* window_ = nullptr;
};
} // namespace fire_engine
