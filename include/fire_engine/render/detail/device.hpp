#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class Glfw;
class Window;

namespace detail
{
/** @cond INTERNAL */

/* --- Classes --- */

/**
 * @brief Owns the Vulkan instance, surface, selected device, and queues.
 *
 * Member declaration order mirrors the Vulkan lifetime dependencies so RAII
 * destroys each object safely in reverse.
 */
class Device final
{
public:
    /**
     * @brief Creates the Vulkan instance, surface, selected device, and queues.
     * @param glfw Initialized GLFW owner used to discover platform extensions.
     * @param window Window for which presentation support and a surface are required.
     * @param applicationName Null-terminated name reported to the Vulkan runtime.
     * @throws std::runtime_error if the runtime or available devices are unsuitable.
     * @throws vk::SystemError if a Vulkan operation fails.
     */
    Device(const Glfw& glfw, const Window& window, const std::string& applicationName);

    /** @brief Releases every Vulkan resource in dependency-safe reverse order. */
    ~Device() = default;

    /// @brief Copy construction is disabled because Vulkan handles have unique ownership.
    Device(const Device&) = delete;
    /// @brief Copy assignment is disabled because Vulkan handles have unique ownership.
    Device& operator=(const Device&) = delete;
    /// @brief Move construction is disabled so dependent handle addresses remain stable.
    Device(Device&&) = delete;
    /// @brief Move assignment is disabled so dependent handle addresses remain stable.
    Device& operator=(Device&&) = delete;

    /**
     * @brief Returns the Vulkan-reported name of the selected physical device.
     * @return A copy of the physical device name.
     */
    [[nodiscard]] std::string name() const;

    /**
     * @brief Returns the Vulkan instance used by this device setup.
     * @return Reference to the owned instance.
     */
    [[nodiscard]] const vk::raii::Instance& instance() const noexcept;

    /**
     * @brief Returns the presentation surface associated with the window.
     * @return Reference to the owned surface.
     */
    [[nodiscard]] const vk::raii::SurfaceKHR& surface() const noexcept;

    /**
     * @brief Returns the selected physical device.
     * @return Reference to the selected physical device.
     */
    [[nodiscard]] const vk::raii::PhysicalDevice& physicalDevice() const noexcept;

    /**
     * @brief Returns the logical device used to create render resources.
     * @return Reference to the owned logical device.
     */
    [[nodiscard]] const vk::raii::Device& logicalDevice() const noexcept;

    /**
     * @brief Returns the queue-family index used for graphics commands.
     * @return Selected graphics queue-family index.
     */
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const noexcept;

    /**
     * @brief Returns the queue-family index used for presentation.
     * @return Selected presentation queue-family index.
     */
    [[nodiscard]] std::uint32_t presentQueueFamily() const noexcept;

    /**
     * @brief Returns the logical-device queue used for graphics commands.
     * @return Reference to the owned graphics queue.
     */
    [[nodiscard]] const vk::raii::Queue& graphicsQueue() const noexcept;

    /**
     * @brief Returns the logical-device queue used for presentation.
     * @return Reference to the owned presentation queue.
     */
    [[nodiscard]] const vk::raii::Queue& presentQueue() const noexcept;

private:
    /**
     * @brief Creates the Vulkan instance and optional validation messenger.
     * @param glfw Initialized GLFW owner used to obtain platform extensions.
     * @param applicationName Null-terminated name reported to Vulkan.
     * @throws std::runtime_error if the Vulkan loader is older than version 1.4.
     * @throws vk::SystemError if instance or messenger creation fails.
     */
    void createInstance(const Glfw& glfw, const std::string& applicationName);

    /**
     * @brief Vulkan-Hpp context connected to the loader supplied by vcpkg.
     *
     * Passing the loader entry point explicitly avoids Vulkan-Hpp's optional
     * dlopen helper and any dependency on a Vulkan SDK environment.
     */
    vk::raii::Context context_{vkGetInstanceProcAddr};
    vk::raii::Instance instance_{nullptr}; ///< Vulkan instance owned by this device setup.
    vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr}; ///< Optional validation messenger.
    vk::raii::SurfaceKHR surface_{nullptr}; ///< Presentation surface associated with the window.
    vk::raii::PhysicalDevice physicalDevice_{nullptr}; ///< Selected physical device.
    vk::raii::Device logicalDevice_{nullptr}; ///< Logical interface to the selected device.
    vk::raii::Queue graphicsQueue_{nullptr};  ///< Queue used for graphics commands.
    vk::raii::Queue presentQueue_{nullptr};   ///< Queue used to present rendered images.
    std::uint32_t graphicsQueueFamily_ = 0;   ///< Family index of graphicsQueue_.
    std::uint32_t presentQueueFamily_ = 0;    ///< Family index of presentQueue_.
    bool hasKhrSurfaceMaintenance1_ = false;  ///< Whether the KHR dependency was enabled.
    bool hasExtSurfaceMaintenance1_ = false;  ///< Whether the EXT dependency was enabled.
};
/** @endcond */
} // namespace detail
} // namespace fire_engine
