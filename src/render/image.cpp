#include <fire_engine/render/detail/image.hpp>

#include <fire_engine/render/detail/allocator.hpp>

#include <stdexcept>
#include <string>

#include <vk_mem_alloc.h>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

AllocatedImage::AllocatedImage(const MemoryAllocator& allocator, std::uint32_t width,
                               std::uint32_t height, vk::Format format, vk::ImageUsageFlags usage)
    : allocator_{allocator.handle()}
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("A Vulkan image cannot have a zero-sized extent");
    }

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = static_cast<VkFormat>(format),
        .extent = {.width = width, .height = height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = static_cast<VkImageUsageFlags>(usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage image = VK_NULL_HANDLE;
    const VkResult result =
        vmaCreateImage(allocator_, &imageInfo, &allocationInfo, &image, &allocation_, nullptr);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("VMA image creation failed: " +
                                 vk::to_string(static_cast<vk::Result>(result)));
    }
    image_ = image;
}

AllocatedImage::~AllocatedImage()
{
    vmaDestroyImage(allocator_, static_cast<VkImage>(image_), allocation_);
}

vk::Image AllocatedImage::handle() const noexcept
{
    return image_;
}
/** @endcond */
} // namespace fire_engine::detail
