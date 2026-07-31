#include <fire_engine/render/device.hpp>

#include <fire_engine/core/debug.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fire_engine
{
namespace
{
/* --- File-local constants --- */

// Highest scheduler priority within the family. This is a relative Vulkan
// priority, not an operating-system one. Static storage ensures the pointers
// retained by queue create infos remain valid until vkCreateDevice reads them.
constexpr float kQueuePriority = 1.0F;
constexpr std::uint32_t kVersion = VK_MAKE_API_VERSION(
    0, FIRE_ENGINE_VERSION_MAJOR, FIRE_ENGINE_VERSION_MINOR, FIRE_ENGINE_VERSION_PATCH);

/* --- File-local types --- */

// Graphics work and presentation are capabilities of queue families. They are
// often the same family, but Vulkan permits a platform to expose them separately.
struct QueueFamilies
{
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
};

struct DeviceSelection
{
    vk::raii::PhysicalDevice physicalDevice;
    std::uint32_t graphicsQueueFamily;
    std::uint32_t presentQueueFamily;
};

/* --- File-local function declarations --- */

[[nodiscard]] QueueFamilies findQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice,
                                              const vk::raii::SurfaceKHR& surface);
[[nodiscard]] bool supportsSwapchain(const vk::raii::PhysicalDevice& physicalDevice);
[[nodiscard]] std::expected<DeviceSelection, std::string>
inspectDevice(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::SurfaceKHR& surface);
[[nodiscard]] DeviceSelection choosePhysicalDevice(const vk::raii::Instance& instance,
                                                   const vk::raii::SurfaceKHR& surface);
[[nodiscard]] std::vector<vk::DeviceQueueCreateInfo>
makeQueueCreateInfos(const DeviceSelection& selection);
[[nodiscard]] vk::raii::Device createLogicalDeviceFor(const DeviceSelection& selection);
} // namespace

/* --- Public member functions --- */

Device::Device(const Glfw& glfw, const Window& window, const std::string& applicationName)
{
    // Keep the dependent Vulkan initialization steps visible in one place: the
    // surface belongs to the instance, device selection depends on that surface,
    // and the logical device and queues depend on the selected physical device.
    createInstance(glfw, applicationName);
    surface_ = window.createVulkanSurface(instance_);

    const DeviceSelection selection = choosePhysicalDevice(instance_, surface_);
    physicalDevice_ = selection.physicalDevice;
    graphicsQueueFamily_ = selection.graphicsQueueFamily;
    presentQueueFamily_ = selection.presentQueueFamily;

    logicalDevice_ = createLogicalDeviceFor(selection);
    graphicsQueue_ = logicalDevice_.getQueue(graphicsQueueFamily_, 0);
    presentQueue_ = logicalDevice_.getQueue(presentQueueFamily_, 0);
}

std::string Device::name() const
{
    return physicalDevice_.getProperties().deviceName.data();
}

std::uint32_t Device::graphicsQueueFamily() const noexcept
{
    return graphicsQueueFamily_;
}

std::uint32_t Device::presentQueueFamily() const noexcept
{
    return presentQueueFamily_;
}

const vk::raii::Queue& Device::graphicsQueue() const noexcept
{
    return graphicsQueue_;
}

const vk::raii::Queue& Device::presentQueue() const noexcept
{
    return presentQueue_;
}

/* --- Private member functions --- */

void Device::createInstance(const Glfw& glfw, const std::string& applicationName)
{
    // The loader version is independent of the installed physical devices. Both
    // are checked: here for the loader, and later in inspectDevice for the GPU.
    if (context_.enumerateInstanceVersion() < vk::ApiVersion14)
    {
        throw std::runtime_error("The Vulkan loader does not support Vulkan 1.4");
    }

    // Start with the platform surface extensions GLFW requires, then opt into
    // debug utilities and validation only when the runtime advertises them.
    const debug::InstanceSupport debugSupport = debug::queryInstanceSupport(context_);
    std::vector<const char*> extensions = glfw.requiredVulkanExtensions();
    if (debugSupport.hasDebugUtils)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    const std::vector<const char*> layers =
        debugSupport.hasValidationLayer ? std::vector<const char*>{debug::kValidationLayerName}
                                        : std::vector<const char*>{};
    const vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo = debug::makeMessengerCreateInfo();

    // std::string supplies the null terminator required by Vulkan. The pointer
    // only needs to remain valid for the synchronous vkCreateInstance call.
    // There is no engine layered beneath this application yet, so the engine
    // name and version stay empty rather than repeating the application's.
    const vk::ApplicationInfo applicationInfo{
        .pApplicationName = applicationName.c_str(),
        .applicationVersion = kVersion,
        .pEngineName = "No Engine",
        .engineVersion = 0,
        .apiVersion = vk::ApiVersion14,
    };
    const vk::InstanceCreateInfo instanceCreateInfo{
        .pNext = debugSupport.hasDebugUtils ? &debugCreateInfo : nullptr,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    // The debug create info is also chained into instance creation so validation
    // can report messages emitted before the messenger itself exists.
    instance_ = vk::raii::Instance{context_, instanceCreateInfo};
    if (debugSupport.hasDebugUtils)
    {
        debugMessenger_ = vk::raii::DebugUtilsMessengerEXT{instance_, debugCreateInfo};
    }
}

namespace
{
/* --- File-local functions --- */

[[nodiscard]] QueueFamilies findQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice,
                                              const vk::raii::SurfaceKHR& surface)
{
    QueueFamilies result;

    // Prefer one family that can perform both jobs. This keeps swapchain images
    // in exclusive sharing mode without queue-family ownership transfers. Keep
    // the first separate candidates as a fallback for devices that need them.
    const auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (std::uint32_t index = 0; index < queueFamilies.size(); ++index)
    {
        const bool supportsGraphics =
            static_cast<bool>(queueFamilies[index].queueFlags & vk::QueueFlagBits::eGraphics);
        const bool supportsPresentation =
            physicalDevice.getSurfaceSupportKHR(index, *surface) == vk::True;

        if (supportsGraphics && supportsPresentation)
        {
            return {.graphics = index, .present = index};
        }
        if (supportsGraphics && !result.graphics)
        {
            result.graphics = index;
        }
        if (supportsPresentation && !result.present)
        {
            result.present = index;
        }
    }
    return result;
}

[[nodiscard]] bool supportsSwapchain(const vk::raii::PhysicalDevice& physicalDevice)
{
    // Presentation requires VK_KHR_swapchain even though the application and
    // device otherwise target the Vulkan 1.4 core API.
    const auto extensions = physicalDevice.enumerateDeviceExtensionProperties();
    return std::ranges::any_of(extensions,
                               [](const vk::ExtensionProperties& extension)
                               {
                                   return std::string_view{extension.extensionName.data()} ==
                                          VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                               });
}

[[nodiscard]] std::expected<DeviceSelection, std::string>
inspectDevice(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::SurfaceKHR& surface)
{
    const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    const char* deviceName = properties.deviceName.data();

    // Requesting Vulkan 1.4 at instance creation does not guarantee that every
    // physical device supports it, so reject older devices explicitly.
    if (properties.apiVersion < vk::ApiVersion14)
    {
        return std::unexpected(std::format("{}: reports Vulkan {}.{}, requires 1.4", deviceName,
                                           vk::apiVersionMajor(properties.apiVersion),
                                           vk::apiVersionMinor(properties.apiVersion)));
    }

    // Vulkan 1.3 mandates both features, so a conformant Vulkan 1.4 driver
    // reports them as supported. Keep the checks for defensive diagnostics with
    // preview drivers and to show that querying support is separate from enabling
    // the features during logical-device creation.
    const auto features =
        physicalDevice
            .getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
    const auto& features13 = features.get<vk::PhysicalDeviceVulkan13Features>();
    if (features13.dynamicRendering != vk::True)
    {
        return std::unexpected(std::format("{}: dynamic rendering is unavailable", deviceName));
    }
    if (features13.synchronization2 != vk::True)
    {
        return std::unexpected(std::format("{}: synchronization2 is unavailable", deviceName));
    }

    // Finish the suitability check with the WSI capabilities needed by the next
    // swapchain milestone.
    const QueueFamilies queueFamilies = findQueueFamilies(physicalDevice, surface);
    if (!queueFamilies.graphics)
    {
        return std::unexpected(
            std::format("{}: no graphics-capable queue family is available", deviceName));
    }
    if (!queueFamilies.present)
    {
        return std::unexpected(
            std::format("{}: no queue family can present to this surface", deviceName));
    }
    if (!supportsSwapchain(physicalDevice))
    {
        return std::unexpected(
            std::format("{}: {} is unavailable", deviceName, VK_KHR_SWAPCHAIN_EXTENSION_NAME));
    }
    if (physicalDevice.getSurfaceFormatsKHR(*surface).empty())
    {
        return std::unexpected(
            std::format("{}: this surface has no supported formats", deviceName));
    }
    if (physicalDevice.getSurfacePresentModesKHR(*surface).empty())
    {
        return std::unexpected(
            std::format("{}: this surface has no supported presentation modes", deviceName));
    }

    return DeviceSelection{
        .physicalDevice = physicalDevice,
        .graphicsQueueFamily = queueFamilies.graphics.value(),
        .presentQueueFamily = queueFamilies.present.value(),
    };
}

[[nodiscard]] DeviceSelection choosePhysicalDevice(const vk::raii::Instance& instance,
                                                   const vk::raii::SurfaceKHR& surface)
{
    std::optional<DeviceSelection> bestDevice;
    std::uint32_t bestScore = 0;
    std::vector<std::string> rejectionReasons;

    const std::vector<vk::raii::PhysicalDevice> physicalDevices =
        instance.enumeratePhysicalDevices();
    if (physicalDevices.empty())
    {
        throw std::runtime_error("No Vulkan physical devices were found");
    }

    // Keep selection deterministic and prefer a discrete GPU when more than one
    // suitable device is installed. Integrated and software devices remain valid.
    for (const vk::raii::PhysicalDevice& physicalDevice : physicalDevices)
    {
        std::expected<DeviceSelection, std::string> candidate =
            inspectDevice(physicalDevice, surface);
        if (!candidate)
        {
            rejectionReasons.push_back(std::move(candidate.error()));
            continue;
        }

        const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
        const std::uint32_t score =
            properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ? 2U : 1U;
        if (!bestDevice || score > bestScore)
        {
            bestDevice = std::move(candidate).value();
            bestScore = score;
        }
    }

    if (!bestDevice)
    {
        std::string message = "No suitable Vulkan physical device was found:";
        for (const std::string& reason : rejectionReasons)
        {
            message += std::format("\n  - {}", reason);
        }
        throw std::runtime_error(message);
    }
    return bestDevice.value();
}

[[nodiscard]] std::vector<vk::DeviceQueueCreateInfo>
makeQueueCreateInfos(const DeviceSelection& selection)
{
    // VkDeviceQueueCreateInfo entries must name unique queue families. If one
    // family handles both graphics and presentation, request it only once.
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.push_back({
        .queueFamilyIndex = selection.graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &kQueuePriority,
    });
    if (selection.presentQueueFamily != selection.graphicsQueueFamily)
    {
        queueCreateInfos.push_back({
            .queueFamilyIndex = selection.presentQueueFamily,
            .queueCount = 1,
            .pQueuePriorities = &kQueuePriority,
        });
    }
    return queueCreateInfos;
}

[[nodiscard]] vk::raii::Device createLogicalDeviceFor(const DeviceSelection& selection)
{
    const std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos = makeQueueCreateInfos(selection);
    constexpr std::array kRequiredDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Querying support does not enable features. Enable only the Vulkan 1.3
    // features that the first rendering milestones will use.
    constexpr vk::PhysicalDeviceVulkan13Features enabledFeatures13{
        .synchronization2 = vk::True,
        .dynamicRendering = vk::True,
    };

    const vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &enabledFeatures13,
        .queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(kRequiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = kRequiredDeviceExtensions.data(),
    };
    return {selection.physicalDevice, deviceCreateInfo};
}
} // namespace
} // namespace fire_engine
