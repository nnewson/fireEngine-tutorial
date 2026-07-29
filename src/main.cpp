#include <exception>
#include <iostream>

#include <vulkan/vulkan_raii.hpp>

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

    // KosmicKrisp is a conformant Vulkan implementation, so it neither needs
    // nor supports the portability-enumeration extension used by MoltenVK.
    // Selecting KosmicKrisp is left to the Vulkan loader configuration.
    const vk::InstanceCreateInfo instanceCreateInfo{
        .pApplicationInfo = &applicationInfo,
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
