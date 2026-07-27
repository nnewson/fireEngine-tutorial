#include <array>
#include <cstdint>
#include <exception>
#include <iostream>

#include <vulkan/vulkan_raii.hpp>

namespace
{

// These are the platform-dependent fields in vk::InstanceCreateInfo. Empty
// defaults mean that conformant Vulkan platforms require no special setup.
struct InstancePlatformOptions
{
    vk::InstanceCreateFlags flags{};
    std::uint32_t enabledExtensionCount{};
    const char* const* ppEnabledExtensionNames{};
};

[[nodiscard]]
InstancePlatformOptions makeInstancePlatformOptions()
{
#if defined(__APPLE__)
    // MoltenVK identifies itself to the Vulkan loader as a portability driver.
    // The loader ignores such drivers unless the application explicitly opts
    // into portability enumeration with both this extension and this flag.
    //
    // This array is static because the returned pointer must remain valid until
    // vkCreateInstance reads it later in main.
    static constexpr std::array enabledExtensions{
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
    };

    return InstancePlatformOptions{
        .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
        .enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size()),
        .ppEnabledExtensionNames = enabledExtensions.data(),
    };
#else
    // Value initialization returns empty flags, zero extensions, and nullptr.
    return InstancePlatformOptions{};
#endif
}

} // namespace

// This is a function try block: the try applies to the entire function body.
// For main(), it is a compact way to turn any startup exception into a useful
// error message and a non-zero process exit code.
int main()
try
{
    // Context loads the Vulkan entry points exposed by the linked Vulkan
    // loader. It must outlive every RAII Vulkan object created from it.
    vk::raii::Context context;

    // Tell the loader that this application targets the Vulkan 1.4 API. This
    // does not select or create a physical GPU; it only sets the API version
    // used when negotiating creation of the instance.
    constexpr vk::ApplicationInfo applicationInfo{
        .pApplicationName = "Fire Engine Vulkan Tutorial",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = vk::ApiVersion14,
    };

    // Ask the platform helper only for the fields that vary by operating
    // system, then keep construction of the Vulkan structure here.
    const auto [flags, enabledExtensionCount, ppEnabledExtensionNames] =
        makeInstancePlatformOptions();

    const vk::InstanceCreateInfo instanceCreateInfo{
        .flags = flags,
        .pApplicationInfo = &applicationInfo,
        .enabledExtensionCount = enabledExtensionCount,
        .ppEnabledExtensionNames = ppEnabledExtensionNames,
    };

    {
        // Constructing the RAII object calls vkCreateInstance. The variable is
        // intentionally never queried: owning the instance for this scope is
        // its entire purpose, hence [[maybe_unused]]. When the scope ends, its
        // destructor calls vkDestroyInstance automatically.
        [[maybe_unused]] const vk::raii::Instance instance{context, instanceCreateInfo};
        std::cout << "Vulkan 1.4 instance created.\n";
    }

    std::cout << "Vulkan instance destroyed.\n";
    return 0;
}
catch (const std::exception& error)
{
    // Vulkan-Hpp reports loader and Vulkan failures as C++ exceptions. Keep
    // those from escaping main and make the smoke test fail clearly instead.
    std::cerr << "Vulkan instance creation failed: " << error.what() << '\n';
    return 1;
}
