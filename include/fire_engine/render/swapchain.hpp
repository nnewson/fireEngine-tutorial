#pragma once

#include <cstddef>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class Device;
class Window;

/* --- Classes --- */

/**
 * @brief Owns a presentation swapchain and views of its presentable images.
 *
 * Vulkan owns the swapchain images. This class owns only the swapchain and one
 * image view for each image, with member order ensuring the views are destroyed
 * before their parent swapchain.
 */
class Swapchain final
{
public:
    /**
     * @brief Creates a swapchain suited to the selected device and current window.
     * @param device Device, surface, and queue families used by the swapchain.
     * @param window Window whose framebuffer size determines the image extent.
     * @throws std::runtime_error if the surface no longer supports presentation.
     * @throws vk::SystemError if swapchain or image-view creation fails.
     */
    Swapchain(const Device& device, const Window& window);

    /** @brief Releases image views followed by their parent swapchain. */
    ~Swapchain() = default;

    /// @brief Copy construction is disabled because Vulkan handles have unique ownership.
    Swapchain(const Swapchain&) = delete;
    /// @brief Copy assignment is disabled because Vulkan handles have unique ownership.
    Swapchain& operator=(const Swapchain&) = delete;
    /// @brief Move construction is disabled so dependent image views remain grouped.
    Swapchain(Swapchain&&) = delete;
    /// @brief Move assignment is disabled so dependent image views remain grouped.
    Swapchain& operator=(Swapchain&&) = delete;

    /**
     * @brief Returns the number of presentable images supplied by Vulkan.
     * @return Number of swapchain images and corresponding image views.
     */
    [[nodiscard]] std::size_t imageCount() const noexcept;

    /**
     * @brief Returns the pixel format shared by every swapchain image.
     * @return Selected surface image format.
     */
    [[nodiscard]] vk::Format imageFormat() const noexcept;

    /**
     * @brief Returns the scheduling mode selected for presentation.
     * @return Mailbox when supported, otherwise Vulkan's guaranteed FIFO mode.
     */
    [[nodiscard]] vk::PresentModeKHR presentMode() const noexcept;

    /**
     * @brief Returns the dimensions of every swapchain image.
     * @return Selected image extent in physical pixels.
     */
    [[nodiscard]] vk::Extent2D extent() const noexcept;

    /**
     * @brief Returns the presentable images owned by the swapchain.
     * @return Non-owning image handles in swapchain order.
     */
    [[nodiscard]] const std::vector<vk::Image>& images() const noexcept;

    /**
     * @brief Returns one owned color view for each swapchain image.
     * @return Image views in the same order as images().
     */
    [[nodiscard]] const std::vector<vk::raii::ImageView>& imageViews() const noexcept;

private:
    vk::raii::SwapchainKHR swapchain_{nullptr};   ///< Presentation swapchain owned by this object.
    std::vector<vk::Image> images_;               ///< Non-owning images supplied by swapchain_.
    std::vector<vk::raii::ImageView> imageViews_; ///< Views destroyed before swapchain_.
    vk::Format imageFormat_ = vk::Format::eUndefined; ///< Format shared by all swapchain images.
    vk::PresentModeKHR presentMode_ = vk::PresentModeKHR::eFifo; ///< Presentation scheduling mode.
    vk::Extent2D extent_{}; ///< Dimensions shared by all images.
};
} // namespace fire_engine
