#include <fire_engine/render/allocator.hpp>

#include <fire_engine/render/device.hpp>

#include <stdexcept>
#include <string>

#include <vulkan/vulkan.hpp>

// The project links the vcpkg Vulkan loader, so VMA can call its exported
// functions directly instead of loading a second set of function pointers.
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

// VMA is a single-header library whose implementation must appear in exactly
// one translation unit. Every other source sees declarations through this file's
// public owner rather than defining the implementation again.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace fire_engine
{
/* --- Public member functions --- */

MemoryAllocator::MemoryAllocator(const Device& device)
{
    // VMA needs the complete Vulkan ownership chain and must not outlive any of
    // these handles. Explicitly reporting 1.4 lets it use appropriate core APIs.
    //
    // VmaAllocatorCreateInfo is a versioned C structure with many optional
    // fields. A partial designated initializer makes Clang diagnose every
    // intentionally omitted field. Value-initialize the complete structure
    // instead, then assign only the settings this tutorial stage uses. That keeps
    // optional values, including flags, at zero until they are actually needed.
    VmaAllocatorCreateInfo createInfo{};
    createInfo.physicalDevice = static_cast<VkPhysicalDevice>(*device.physicalDevice());
    createInfo.device = static_cast<VkDevice>(*device.logicalDevice());
    createInfo.instance = static_cast<VkInstance>(*device.instance());
    createInfo.vulkanApiVersion = VK_API_VERSION_1_4;

    const VkResult result = vmaCreateAllocator(&createInfo, &allocator_);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("VMA allocator creation failed: " +
                                 vk::to_string(static_cast<vk::Result>(result)));
    }
}

MemoryAllocator::~MemoryAllocator()
{
    vmaDestroyAllocator(allocator_);
}

VmaAllocator_T* MemoryAllocator::handle() const noexcept
{
    return allocator_;
}
} // namespace fire_engine
