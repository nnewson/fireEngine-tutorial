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
 * @brief Owns a presentation swapchain and the per-image state that follows it.
 *
 * Vulkan owns the swapchain images. This class owns the swapchain, one image
 * view for each image, and one render-finished semaphore for each image, with
 * member order ensuring the views are destroyed before their parent swapchain.
 *
 * The render-finished semaphores live here rather than with a frame because
 * they are indexed by acquired image. A semaphore signaled for one image may
 * still have an unfinished presentation waiting on it when a later frame comes
 * around, and re-signaling a binary semaphore in that state is invalid. Tying
 * each one to an image makes reuse impossible until that image is acquired
 * again. It also means that once swapchain recreation is introduced, these will
 * be replaced alongside the images whose count may have changed.
 */
class Swapchain final
{
public:
    /**
     * @brief Creates a swapchain suited to the selected device and current window.
     * @param device Device, surface, and queue families used by the swapchain.
     * @param window Window whose framebuffer size determines the image extent.
     * @throws std::runtime_error if the surface no longer supports presentation.
     * @throws vk::SystemError if swapchain, image-view, or semaphore creation fails.
     */
    Swapchain(const Device& device, const Window& window);

    /**
     * @brief Releases image views before the swapchain that supplied their images.
     *
     * That is the only ordering member layout has to enforce. The semaphores'
     * Vulkan parent is the device rather than the swapchain, so their position
     * is free. What they require instead is a precondition no member order can
     * express: no presentation operation may still be using one when it is
     * destroyed. A failed presentation does not lift that requirement, because
     * an out-of-date result still enqueues the queue operation and its
     * semaphore wait.
     *
     * This tutorial destroys the swapchain only at shutdown, after a device
     * wait. That is the conventional fallback rather than a strict guarantee
     * that the presentation engine has released its references. Recreation must
     * keep the retired swapchain and its semaphores alive until their
     * presentation use is known to have finished, or use presentation fences
     * supplied by VK_KHR_swapchain_maintenance1.
     */
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
     * @brief Returns the swapchain used for image acquisition and presentation.
     * @return Reference to the owned Vulkan swapchain.
     */
    [[nodiscard]] const vk::raii::SwapchainKHR& handle() const noexcept;

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
     * @brief Returns one presentable image by its acquired index.
     * @param imageIndex Index returned by Vulkan image acquisition.
     * @return Non-owning swapchain image handle.
     * @throws std::out_of_range if imageIndex is not present in this swapchain.
     */
    [[nodiscard]] vk::Image image(std::size_t imageIndex) const;

    /**
     * @brief Returns one owned color view for each swapchain image.
     * @return Image views in the same order as images().
     */
    [[nodiscard]] const std::vector<vk::raii::ImageView>& imageViews() const noexcept;

    /**
     * @brief Returns the color view belonging to one presentable image.
     * @param imageIndex Index returned by Vulkan image acquisition.
     * @return Owned image view associated with that image.
     * @throws std::out_of_range if imageIndex is not present in this swapchain.
     */
    [[nodiscard]] const vk::raii::ImageView& imageView(std::size_t imageIndex) const;

    /**
     * @brief Returns presentation semaphores in swapchain-image order.
     *
     * The graphics submission for a frame signals the entry belonging to the
     * image that frame acquired, and presentation of that image waits on the
     * same entry.
     *
     * @return One render-finished binary semaphore for each swapchain image.
     */
    [[nodiscard]] const std::vector<vk::raii::Semaphore>& renderFinished() const noexcept;

    /**
     * @brief Returns the presentation semaphore belonging to one swapchain image.
     * @param imageIndex Index returned by Vulkan image acquisition.
     * @return Binary semaphore signaled before that image is presented.
     * @throws std::out_of_range if imageIndex is not present in this swapchain.
     */
    [[nodiscard]] const vk::raii::Semaphore& renderFinished(std::size_t imageIndex) const;

private:
    vk::raii::SwapchainKHR swapchain_{nullptr};   ///< Presentation swapchain owned by this object.
    std::vector<vk::Image> images_;               ///< Non-owning images supplied by swapchain_.
    std::vector<vk::raii::ImageView> imageViews_; ///< Views destroyed before swapchain_.
    std::vector<vk::raii::Semaphore> renderFinished_; ///< One presentation wait per image.
    vk::Format imageFormat_ = vk::Format::eUndefined; ///< Format shared by all swapchain images.
    vk::PresentModeKHR presentMode_ = vk::PresentModeKHR::eFifo; ///< Presentation scheduling mode.
    vk::Extent2D extent_{}; ///< Dimensions shared by all images.
};
} // namespace fire_engine
