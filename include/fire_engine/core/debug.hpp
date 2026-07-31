#pragma once

#include <vulkan/vulkan_raii.hpp>

/** @brief Debugging and Vulkan validation support. */
namespace fire_engine::debug
{
/* --- Constants --- */

/**
 * @brief Name of the standard Khronos validation layer.
 *
 * Validation is a development aid rather than a runtime requirement. Keeping
 * its well-known layer name here lets instance setup enable it only when the
 * user's Vulkan installation actually provides it.
 */
inline constexpr char kValidationLayerName[] = "VK_LAYER_KHRONOS_validation";

/* --- Types --- */

/** @brief Optional validation facilities advertised by the Vulkan runtime. */
struct InstanceSupport
{
    bool hasValidationLayer = false; ///< Whether the Khronos validation layer is installed.
    bool hasDebugUtils = false;      ///< Whether VK_EXT_debug_utils is available.
};

/* --- Functions --- */

/**
 * @brief Queries the validation layer and debug utility support available to an instance.
 * @param context Vulkan context used to enumerate instance layers and extensions.
 * @return The optional validation facilities available to this process.
 * @throws vk::SystemError if Vulkan cannot enumerate the requested properties.
 */
[[nodiscard]] InstanceSupport queryInstanceSupport(const vk::raii::Context& context);

/**
 * @brief Creates the configuration used by the validation debug messenger.
 *
 * The same create info can be chained into instance creation and used to create
 * the messenger, ensuring messages are captured throughout the instance lifetime.
 *
 * @return A configuration that requests warning and error messages.
 */
[[nodiscard]] vk::DebugUtilsMessengerCreateInfoEXT makeMessengerCreateInfo();
} // namespace fire_engine::debug
