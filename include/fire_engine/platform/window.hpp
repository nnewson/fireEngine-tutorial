#pragma once

#include <string>

#include <vulkan/vulkan_raii.hpp>

/* --- External forward declarations --- */

struct GLFWwindow;

namespace fire_engine
{
/* --- Classes --- */

/**
 * @brief Owns a native window and hides GLFW's platform-specific surface work.
 */
class Window final
{
public:
    /**
     * @brief Creates a window without an OpenGL client context.
     * @param width Initial width in screen coordinates; must be positive.
     * @param height Initial height in screen coordinates; must be positive.
     * @param title Null-terminated title displayed by the window system.
     * @throws std::invalid_argument if either dimension is not positive.
     * @throws std::runtime_error if GLFW cannot create the native window.
     */
    Window(int width, int height, const std::string& title);

    /** @brief Destroys the native GLFW window. */
    ~Window();

    /// @brief Copy construction is disabled because the native window has one owner.
    Window(const Window&) = delete;
    /// @brief Copy assignment is disabled because the native window has one owner.
    Window& operator=(const Window&) = delete;
    /// @brief Move construction is disabled so surface lifetime ordering stays explicit.
    Window(Window&&) = delete;
    /// @brief Move assignment is disabled so surface lifetime ordering stays explicit.
    Window& operator=(Window&&) = delete;

    /**
     * @brief Creates a Vulkan surface for this window.
     *
     * The returned RAII surface is owned by Vulkan and must be destroyed before
     * its instance. It does not own this Window.
     *
     * @param instance Vulkan instance that will own the surface.
     * @return An exception-safe Vulkan surface wrapper.
     * @throws std::runtime_error if GLFW cannot create the platform surface.
     */
    [[nodiscard]] vk::raii::SurfaceKHR
    createVulkanSurface(const vk::raii::Instance& instance) const;

    /**
     * @brief Returns the drawable framebuffer size in physical pixels.
     * @return Current framebuffer extent; either dimension may be zero while minimized.
     */
    [[nodiscard]] vk::Extent2D framebufferExtent() const noexcept;

    /**
     * @brief Reports whether the user or window system has requested closure.
     * @return True when the application event loop should finish.
     */
    [[nodiscard]] bool shouldClose() const noexcept;

    /** @brief Processes pending events for all GLFW windows on the current thread. */
    void pollEvents() const noexcept;

    /** @brief Sleeps until the window system delivers at least one event. */
    void waitEvents() const noexcept;

    /**
     * @brief Reports and clears a framebuffer-size notification.
     * @return True once after each framebuffer-size callback.
     */
    [[nodiscard]] bool consumeFramebufferResize() noexcept;

private:
    /**
     * @brief Records a framebuffer-size callback for the owning Window.
     * @param window Native GLFW window carrying this instance as its user pointer.
     * @param width Latest framebuffer width reported by GLFW.
     * @param height Latest framebuffer height reported by GLFW.
     */
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height) noexcept;

    GLFWwindow* window_ = nullptr;    ///< Native window owned by this object.
    bool framebufferResized_ = false; ///< Set by GLFW until consumed by the event loop.
};
} // namespace fire_engine
