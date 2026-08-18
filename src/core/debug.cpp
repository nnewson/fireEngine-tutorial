#include <fire_engine/core/detail/debug.hpp>

#include <algorithm>
#include <string_view>

#include <fire_engine/core/log.hpp>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local function declarations --- */

VKAPI_ATTR vk::Bool32 VKAPI_CALL
debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT,
              const vk::DebugUtilsMessengerCallbackDataEXT* callbackData, void*) noexcept;
/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Internal functions --- */

InstanceSupport queryInstanceSupport(const vk::raii::Context& context)
{
    InstanceSupport support;

    const auto extensions = context.enumerateInstanceExtensionProperties();
    support.hasSurfaceCapabilities2 =
        std::ranges::any_of(extensions,
                            [](const vk::ExtensionProperties& extension)
                            {
                                return std::string_view{extension.extensionName.data()} ==
                                       VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
                            });
    support.hasKhrSurfaceMaintenance1 =
        std::ranges::any_of(extensions,
                            [](const vk::ExtensionProperties& extension)
                            {
                                return std::string_view{extension.extensionName.data()} ==
                                       VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
                            });
    support.hasExtSurfaceMaintenance1 =
        std::ranges::any_of(extensions,
                            [](const vk::ExtensionProperties& extension)
                            {
                                return std::string_view{extension.extensionName.data()} ==
                                       VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
                            });

#ifndef FIRE_ENGINE_ENABLE_VALIDATION
    // Release builds skip validation by default. Keeping the function available
    // avoids spreading build-configuration conditionals through Device.
    return support;
#else
    // Layers are installed independently from the loader, so requesting
    // validation unconditionally would make an otherwise valid runtime fail.
    const auto layers = context.enumerateInstanceLayerProperties();
    support.hasValidationLayer = std::ranges::any_of(
        layers, [](const vk::LayerProperties& layer)
        { return std::string_view{layer.layerName.data()} == kValidationLayerName; });

    // The messenger creation call is provided by VK_EXT_debug_utils. Query it
    // separately because a machine may have validation without this extension.
    support.hasDebugUtils =
        std::ranges::any_of(extensions,
                            [](const vk::ExtensionProperties& extension)
                            {
                                return std::string_view{extension.extensionName.data()} ==
                                       VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
                            });

    return support;
#endif
}

vk::DebugUtilsMessengerCreateInfoEXT makeMessengerCreateInfo()
{
    // Warnings and errors keep the tutorial signal useful without printing the
    // much noisier informational and verbose validation streams.
    return {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = debugCallback,
    };
}
/** @endcond */

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

/**
 * @brief Forwards a Vulkan validation message to the engine logger.
 *
 * Vulkan calls this function synchronously when a selected message is emitted.
 * Returning false tells Vulkan that the callback observed the message but does
 * not want to abort the API call that triggered it.
 *
 * @param severity Whether Vulkan classified the message as a warning or error.
 * @param callbackData Validation message data supplied by Vulkan.
 * @return VK_FALSE so the API call that produced the message can continue.
 */
VKAPI_ATTR vk::Bool32 VKAPI_CALL
debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT,
              const vk::DebugUtilsMessengerCallbackDataEXT* callbackData, void*) noexcept
{
    // log is noexcept because C++ exceptions must not escape through Vulkan's
    // C callback boundary.
    const std::string_view severityName =
        severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ? "error" : "warning";
    fire_engine::log("Vulkan validation {}: {}", severityName, callbackData->pMessage);
    return VK_FALSE;
}
/** @endcond */
} // namespace
} // namespace fire_engine::detail
