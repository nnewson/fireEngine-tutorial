#include <fire_engine/render/swapchain.hpp>

#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/device.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace fire_engine
{
namespace
{
/* --- File-local constants --- */

/** @brief Preferred color format for presenting gamma-correct color values. */
constexpr vk::SurfaceFormatKHR kPreferredSurfaceFormat{
    .format = vk::Format::eB8G8R8A8Srgb,
    .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
};

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
[[nodiscard]] vk::SurfaceFormatKHR
chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
[[nodiscard]] vk::PresentModeKHR
choosePresentMode(const std::vector<vk::PresentModeKHR>& presentModes);
[[nodiscard]] vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                                        const Window& window);
[[nodiscard]] std::uint32_t chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities);
[[nodiscard]] vk::CompositeAlphaFlagBitsKHR
chooseCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities);
[[nodiscard]] std::vector<vk::raii::ImageView>
createImageViews(const vk::raii::Device& device, const std::vector<vk::Image>& images,
                 vk::Format format);
[[nodiscard]] std::vector<vk::raii::Semaphore>
createRenderFinishedSemaphores(const vk::raii::Device& device, std::size_t imageCount);
} // namespace

/* --- Public member functions --- */

Swapchain::Swapchain(const Device& device, const Window& window)
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
    const vk::SurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    const vk::PresentModeKHR presentMode = choosePresentMode(support.presentModes);
    const vk::Extent2D imageExtent = chooseExtent(support.capabilities, window);
    const std::array queueFamilies = {device.graphicsQueueFamily(), device.presentQueueFamily()};
    const bool usesSeparateQueueFamilies = queueFamilies[0] != queueFamilies[1];

    // Concurrent sharing avoids explicit ownership transfers when presentation
    // and graphics use different queue families. A shared family can use the
    // more efficient exclusive mode.
    const vk::SwapchainCreateInfoKHR createInfo{
        .surface = *device.surface(),
        .minImageCount = chooseImageCount(support.capabilities),
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
        .compositeAlpha = chooseCompositeAlpha(support.capabilities),
        .presentMode = presentMode,
        .clipped = vk::True,
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

namespace
{
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
 * @brief Chooses an sRGB surface format when available.
 * @param formats Non-empty format and color-space pairs reported by the surface.
 * Device selection rejects any device reporting none, so this is never empty.
 * @return Preferred sRGB pair, or the first supported pair as a fallback.
 */
[[nodiscard]] vk::SurfaceFormatKHR
chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats)
{
    const auto preferred = std::ranges::find(formats, kPreferredSurfaceFormat);
    return preferred != formats.end() ? *preferred : formats.front();
}

/**
 * @brief Chooses low-latency mailbox presentation when supported.
 * @param presentModes Presentation modes reported by the surface.
 * @return Mailbox when available, otherwise Vulkan's guaranteed FIFO mode.
 */
[[nodiscard]] vk::PresentModeKHR
choosePresentMode(const std::vector<vk::PresentModeKHR>& presentModes)
{
    return std::ranges::find(presentModes, vk::PresentModeKHR::eMailbox) != presentModes.end()
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

/**
 * @brief Chooses a swapchain extent compatible with the surface and framebuffer.
 * @param capabilities Surface extent limits and any platform-defined fixed extent.
 * @param window Window whose framebuffer supplies the desired pixel dimensions.
 * @return Fixed surface extent or clamped framebuffer extent.
 * @throws std::runtime_error if the window framebuffer currently has zero area.
 */
[[nodiscard]] vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                                        const Window& window)
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    const vk::Extent2D framebufferExtent = window.framebufferExtent();
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        throw std::runtime_error("The window framebuffer has zero area");
    }
    return {
        .width = std::clamp(framebufferExtent.width, capabilities.minImageExtent.width,
                            capabilities.maxImageExtent.width),
        .height = std::clamp(framebufferExtent.height, capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height),
    };
}

/**
 * @brief Requests one image beyond the surface minimum to reduce pipeline stalls.
 * @param capabilities Surface-supported image-count range.
 * @return Desired image count capped by a finite surface maximum.
 */
[[nodiscard]] std::uint32_t chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    const std::uint32_t desiredCount = capabilities.minImageCount + 1;
    return capabilities.maxImageCount > 0 ? std::min(desiredCount, capabilities.maxImageCount)
                                          : desiredCount;
}

/**
 * @brief Chooses the first conventional alpha-compositing mode supported by the surface.
 * @param capabilities Surface flags describing supported alpha behavior.
 * @return A supported composite-alpha mode, preferring opaque output.
 * @throws std::runtime_error if the surface reports no recognized mode.
 */
[[nodiscard]] vk::CompositeAlphaFlagBitsKHR
chooseCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    constexpr std::array kPreferredModes = {
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
        vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
        vk::CompositeAlphaFlagBitsKHR::eInherit,
    };
    for (const vk::CompositeAlphaFlagBitsKHR mode : kPreferredModes)
    {
        if (static_cast<bool>(capabilities.supportedCompositeAlpha & mode))
        {
            return mode;
        }
    }
    throw std::runtime_error("The presentation surface has no supported composite-alpha mode");
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
} // namespace
} // namespace fire_engine
