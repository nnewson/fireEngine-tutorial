#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class Glfw;
class Window;

/* --- Classes --- */

// Owns the Vulkan objects established by milestone 2. Member declaration order
// mirrors their lifetime dependencies so RAII destroys them safely in reverse.
class Device final
{
public:
    Device(const Glfw& glfw, const Window& window, const std::string& applicationName);
    ~Device() = default;

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    [[nodiscard]] std::string name() const;
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const noexcept;
    [[nodiscard]] std::uint32_t presentQueueFamily() const noexcept;
    [[nodiscard]] const vk::raii::Queue& graphicsQueue() const noexcept;
    [[nodiscard]] const vk::raii::Queue& presentQueue() const noexcept;

private:
    void createInstance(const Glfw& glfw, const std::string& applicationName);

    // vcpkg supplies and links the loader. Passing its entry point explicitly
    // avoids relying on Vulkan-Hpp's optional dlopen helper or SDK environment.
    vk::raii::Context context_{vkGetInstanceProcAddr};
    vk::raii::Instance instance_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr};
    vk::raii::SurfaceKHR surface_{nullptr};
    vk::raii::PhysicalDevice physicalDevice_{nullptr};
    vk::raii::Device logicalDevice_{nullptr};
    vk::raii::Queue graphicsQueue_{nullptr};
    vk::raii::Queue presentQueue_{nullptr};
    std::uint32_t graphicsQueueFamily_ = 0;
    std::uint32_t presentQueueFamily_ = 0;
};
} // namespace fire_engine
