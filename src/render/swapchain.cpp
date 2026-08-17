#include <fire_engine/render/detail/swapchain.hpp>

#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/detail/device.hpp>
#include <fire_engine/render/detail/swapchain_selection.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace fire_engine::detail
{
namespace
{
/** @cond INTERNAL */
/* --- File-local types --- */

/** @brief Surface properties that determine valid swapchain configuration. */
struct SurfaceSupport
{
    vk::SurfaceCapabilitiesKHR capabilities;   ///< Image counts, extents, and supported behavior.
    std::vector<vk::SurfaceFormatKHR> formats; ///< Supported format and color-space pairs.
    std::vector<vk::PresentModeKHR> presentModes; ///< Supported presentation scheduling modes.
};

/* --- File-local function declarations --- */

[[nodiscard]] SurfaceSupport querySurfaceSupport(const Device& device);
[[nodiscard]] std::vector<vk::raii::ImageView>
createImageViews(const vk::raii::Device& device, const std::vector<vk::Image>& images,
                 vk::Format format);
[[nodiscard]] std::vector<vk::raii::Semaphore>
createRenderFinishedSemaphores(const vk::raii::Device& device, std::size_t imageCount);
/** @endcond */
} // namespace

/** @cond INTERNAL */
/* --- Internal member functions --- */

Swapchain::Swapchain(const Device& device, const Window& window, vk::SwapchainKHR oldSwapchain)
{
    const SurfaceSupport support = querySurfaceSupport(device);

    // The specification requires every surface to support color attachments,
    // so a conformant driver always passes. Keep this for the same reason as
    // the Vulkan 1.3 feature checks: preview drivers are not always conformant.
    if (!static_cast<bool>(support.capabilities.supportedUsageFlags &
                           vk::ImageUsageFlagBits::eColorAttachment))
    {
        throw std::runtime_error("The presentation surface does not support color attachments");
    }
    const vk::SurfaceFormatKHR surfaceFormat = detail::chooseSurfaceFormat(support.formats);
    const vk::PresentModeKHR presentMode = detail::choosePresentMode(support.presentModes);
    const vk::Extent2D imageExtent =
        detail::chooseExtent(support.capabilities, window.framebufferExtent());
    const std::array queueFamilies = {device.graphicsQueueFamily(), device.presentQueueFamily()};
    const bool usesSeparateQueueFamilies = queueFamilies[0] != queueFamilies[1];

    // Concurrent sharing avoids explicit ownership transfers when presentation
    // and graphics use different queue families. A shared family can use the
    // more efficient exclusive mode.
    const vk::SwapchainCreateInfoKHR createInfo{
        .surface = *device.surface(),
        .minImageCount = detail::chooseImageCount(support.capabilities),
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = imageExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode =
            usesSeparateQueueFamilies ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        .queueFamilyIndexCount =
            usesSeparateQueueFamilies ? static_cast<std::uint32_t>(queueFamilies.size()) : 0U,
        .pQueueFamilyIndices = usesSeparateQueueFamilies ? queueFamilies.data() : nullptr,
        .preTransform = support.capabilities.currentTransform,
        .compositeAlpha = detail::chooseCompositeAlpha(support.capabilities),
        .presentMode = presentMode,
        .clipped = vk::True,
        .oldSwapchain = oldSwapchain,
    };

    swapchain_ = vk::raii::SwapchainKHR{device.logicalDevice(), createInfo};
    images_ = swapchain_.getImages();
    imageViews_ = createImageViews(device.logicalDevice(), images_, surfaceFormat.format);
    renderFinished_ = createRenderFinishedSemaphores(device.logicalDevice(), images_.size());
    imageFormat_ = surfaceFormat.format;
    presentMode_ = presentMode;
    extent_ = imageExtent;
}

std::size_t Swapchain::imageCount() const noexcept
{
    return images_.size();
}

const vk::raii::SwapchainKHR& Swapchain::handle() const noexcept
{
    return swapchain_;
}

vk::Format Swapchain::imageFormat() const noexcept
{
    return imageFormat_;
}

vk::PresentModeKHR Swapchain::presentMode() const noexcept
{
    return presentMode_;
}

vk::Extent2D Swapchain::extent() const noexcept
{
    return extent_;
}

const std::vector<vk::Image>& Swapchain::images() const noexcept
{
    return images_;
}

vk::Image Swapchain::image(std::size_t imageIndex) const
{
    return images_.at(imageIndex);
}

const std::vector<vk::raii::ImageView>& Swapchain::imageViews() const noexcept
{
    return imageViews_;
}

const vk::raii::ImageView& Swapchain::imageView(std::size_t imageIndex) const
{
    return imageViews_.at(imageIndex);
}

const std::vector<vk::raii::Semaphore>& Swapchain::renderFinished() const noexcept
{
    return renderFinished_;
}

const vk::raii::Semaphore& Swapchain::renderFinished(std::size_t imageIndex) const
{
    return renderFinished_.at(imageIndex);
}
/** @endcond */

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

/**
 * @brief Queries every surface property needed to configure a swapchain.
 * @param device Device and surface whose compatibility is queried.
 * @return Current surface capabilities, formats, and present modes.
 * @throws vk::SystemError if a Vulkan surface query fails.
 */
[[nodiscard]] SurfaceSupport querySurfaceSupport(const Device& device)
{
    return {
        .capabilities = device.physicalDevice().getSurfaceCapabilitiesKHR(*device.surface()),
        .formats = device.physicalDevice().getSurfaceFormatsKHR(*device.surface()),
        .presentModes = device.physicalDevice().getSurfacePresentModesKHR(*device.surface()),
    };
}

/**
 * @brief Creates a two-dimensional color view for every swapchain image.
 * @param device Logical device that owns the image-view objects.
 * @param images Non-owning images supplied by the swapchain.
 * @param format Format shared by every image.
 * @return Owned image views in the same order as the input images.
 * @throws vk::SystemError if Vulkan cannot create an image view.
 */
[[nodiscard]] std::vector<vk::raii::ImageView>
createImageViews(const vk::raii::Device& device, const std::vector<vk::Image>& images,
                 vk::Format format)
{
    std::vector<vk::raii::ImageView> imageViews;
    imageViews.reserve(images.size());
    for (const vk::Image image : images)
    {
        const vk::ImageViewCreateInfo createInfo{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        imageViews.emplace_back(device, createInfo);
    }
    return imageViews;
}

/**
 * @brief Creates one render-finished semaphore for every swapchain image.
 * @param device Logical device that owns the semaphore objects.
 * @param imageCount Number of presentable images supplied by the swapchain.
 * @return Owned binary semaphores in swapchain-image order.
 * @throws vk::SystemError if Vulkan cannot create a semaphore.
 */
[[nodiscard]] std::vector<vk::raii::Semaphore>
createRenderFinishedSemaphores(const vk::raii::Device& device, std::size_t imageCount)
{
    // With no VkSemaphoreTypeCreateInfo in its pNext chain, Vulkan creates a
    // binary semaphore, which is what queue presentation accepts.
    constexpr vk::SemaphoreCreateInfo createInfo{};

    std::vector<vk::raii::Semaphore> semaphores;
    semaphores.reserve(imageCount);
    for (std::size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        semaphores.emplace_back(device, createInfo);
    }
    return semaphores;
}
/** @endcond */
} // namespace
} // namespace fire_engine::detail
