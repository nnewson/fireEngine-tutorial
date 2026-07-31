#pragma once

#include <vector>

namespace fire_engine
{
/* --- Classes --- */

/**
 * @brief Owns GLFW's process-wide initialized state.
 *
 * Construct this before any Window and keep it alive until every GLFW window
 * has been destroyed.
 */
class Glfw final
{
public:
    /**
     * @brief Initializes GLFW and connects it to the linked Vulkan loader.
     * @throws std::runtime_error if GLFW initialization or Vulkan discovery fails.
     */
    Glfw();

    /** @brief Terminates GLFW after all dependent windows have been destroyed. */
    ~Glfw();

    /// @brief Copy construction is disabled because GLFW state has one process-wide owner.
    Glfw(const Glfw&) = delete;
    /// @brief Copy assignment is disabled because GLFW state has one process-wide owner.
    Glfw& operator=(const Glfw&) = delete;
    /// @brief Move construction is disabled so the owner's lifetime remains explicit.
    Glfw(Glfw&&) = delete;
    /// @brief Move assignment is disabled so the owner's lifetime remains explicit.
    Glfw& operator=(Glfw&&) = delete;

    /**
     * @brief Returns the Vulkan WSI extensions required by GLFW's active platform.
     *
     * Examples include VK_EXT_metal_surface on macOS and an X11 surface extension
     * on Linux. The returned names remain owned by GLFW and are valid while this
     * object is alive.
     *
     * @return Vulkan instance extension names required for surface creation.
     * @throws std::runtime_error if GLFW cannot find a usable set of extensions.
     */
    [[nodiscard]] std::vector<const char*> requiredVulkanExtensions() const;
};
} // namespace fire_engine
