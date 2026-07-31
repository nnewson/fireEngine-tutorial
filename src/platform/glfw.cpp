#include <fire_engine/platform/glfw.hpp>

#include <cstdint>
#include <stdexcept>

#include <fire_engine/core/log.hpp>

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace fire_engine
{
namespace
{
/* --- File-local function declarations --- */

void errorCallback(int errorCode, const char* description) noexcept;
} // namespace

/* --- Public member functions --- */

Glfw::Glfw()
{
    // Install the callback before initialization so startup failures include
    // the platform-specific reason reported by GLFW.
    glfwSetErrorCallback(errorCallback);

    // Both GLFW surface creation and Vulkan-Hpp now use the loader linked from
    // vcpkg. This is especially important on macOS, where that loader may not
    // live in a system search path.
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (glfwInit() != GLFW_TRUE)
    {
        throw std::runtime_error("GLFW initialization failed");
    }

    // This verifies that GLFW can reach the linked Vulkan loader and that the
    // loader can see at least one installed driver before we create a window.
    if (glfwVulkanSupported() != GLFW_TRUE)
    {
        glfwTerminate();
        throw std::runtime_error("GLFW could not find a Vulkan loader and ICD");
    }
}

Glfw::~Glfw()
{
    // glfwTerminate releases GLFW's process-wide resources after every Window
    // has already been destroyed by the caller's object lifetime ordering.
    glfwTerminate();
}

std::vector<const char*> Glfw::requiredVulkanExtensions() const
{
    // GLFW selects the surface extensions for the active window backend. Their
    // string storage remains owned by GLFW and valid until this object's destructor
    // calls glfwTerminate.
    std::uint32_t extensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (glfwExtensions == nullptr || extensionCount == 0)
    {
        throw std::runtime_error("GLFW returned no required Vulkan instance extensions");
    }
    return {glfwExtensions, glfwExtensions + extensionCount};
}

namespace
{
/* --- File-local functions --- */

void errorCallback(int errorCode, const char* description) noexcept
{
    // The shared logger is noexcept because C++ exceptions must not escape
    // through GLFW's C callback boundary.
    log("GLFW error {}: {}", errorCode, description);
}
} // namespace
} // namespace fire_engine
