#pragma once

#include <vector>

namespace fire_engine
{
/* --- Classes --- */

// Owns GLFW's process-wide initialized state. Construct this before any Window
// and keep it alive until every GLFW window has been destroyed.
class Glfw final
{
public:
    Glfw();
    ~Glfw();

    Glfw(const Glfw&) = delete;
    Glfw& operator=(const Glfw&) = delete;
    Glfw(Glfw&&) = delete;
    Glfw& operator=(Glfw&&) = delete;

    // GLFW knows which WSI extensions the current operating system needs, such
    // as VK_EXT_metal_surface on macOS or an X11 surface extension on Linux.
    [[nodiscard]] std::vector<const char*> requiredVulkanExtensions() const;
};
} // namespace fire_engine
