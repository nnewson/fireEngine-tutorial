#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine::debug
{
/* --- Constants --- */

// Validation is a development aid rather than a runtime requirement. Keeping
// its well-known layer name here lets instance setup enable it only when the
// user's Vulkan installation actually provides it.
inline constexpr char kValidationLayerName[] = "VK_LAYER_KHRONOS_validation";

/* --- Types --- */

struct InstanceSupport
{
    bool hasValidationLayer = false;
    bool hasDebugUtils = false;
};

/* --- Functions --- */

[[nodiscard]] InstanceSupport queryInstanceSupport(const vk::raii::Context& context);

// Describes which messages Vulkan should route to the callback. The same create
// info can be chained into instance creation and used to create the messenger.
[[nodiscard]] vk::DebugUtilsMessengerCreateInfoEXT makeMessengerCreateInfo();
} // namespace fire_engine::debug
