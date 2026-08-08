#include "fire_engine/render/detail/swapchain_selection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
namespace selection = fire_engine::detail;

[[nodiscard]] vk::SurfaceCapabilitiesKHR variableExtentCapabilities()
{
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.currentExtent = {
        .width = std::numeric_limits<std::uint32_t>::max(),
        .height = std::numeric_limits<std::uint32_t>::max(),
    };
    capabilities.minImageExtent = {.width = 320, .height = 240};
    capabilities.maxImageExtent = {.width = 1920, .height = 1080};
    return capabilities;
}
} // namespace

TEST_CASE("Swapchain format selection prefers sRGB")
{
    const vk::SurfaceFormatKHR fallback{
        .format = vk::Format::eR8G8B8A8Unorm,
        .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
    };
    const vk::SurfaceFormatKHR preferred{
        .format = vk::Format::eB8G8R8A8Srgb,
        .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
    };

    REQUIRE(selection::chooseSurfaceFormat({fallback}) == fallback);
    REQUIRE(selection::chooseSurfaceFormat({fallback, preferred}) == preferred);
}

TEST_CASE("Swapchain present-mode selection prefers mailbox")
{
    REQUIRE(selection::choosePresentMode({vk::PresentModeKHR::eImmediate}) ==
            vk::PresentModeKHR::eFifo);
    REQUIRE(
        selection::choosePresentMode({vk::PresentModeKHR::eFifo, vk::PresentModeKHR::eMailbox}) ==
        vk::PresentModeKHR::eMailbox);
}

TEST_CASE("Swapchain extent selection respects fixed and variable surfaces")
{
    SECTION("fixed extent")
    {
        vk::SurfaceCapabilitiesKHR capabilities{};
        capabilities.currentExtent = {.width = 800, .height = 600};
        REQUIRE(selection::chooseExtent(capabilities, {}) == capabilities.currentExtent);
    }
    SECTION("clamped variable extent")
    {
        const vk::SurfaceCapabilitiesKHR capabilities = variableExtentCapabilities();
        const vk::Extent2D framebufferExtent{.width = 2560, .height = 200};
        const vk::Extent2D expected{.width = 1920, .height = 240};
        REQUIRE(selection::chooseExtent(capabilities, framebufferExtent) == expected);
    }
    SECTION("zero-area variable extent")
    {
        const vk::SurfaceCapabilitiesKHR capabilities = variableExtentCapabilities();
        REQUIRE_THROWS_AS(selection::chooseExtent(capabilities, {}), std::runtime_error);
    }
}

TEST_CASE("Swapchain image-count selection respects finite maxima")
{
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.minImageCount = 2;

    capabilities.maxImageCount = 0;
    REQUIRE(selection::chooseImageCount(capabilities) == 3);

    capabilities.maxImageCount = 2;
    REQUIRE(selection::chooseImageCount(capabilities) == 2);
}

TEST_CASE("Swapchain composite-alpha selection follows preference order")
{
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.supportedCompositeAlpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied |
                                           vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
    REQUIRE(selection::chooseCompositeAlpha(capabilities) ==
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied);

    capabilities.supportedCompositeAlpha = {};
    REQUIRE_THROWS_AS(selection::chooseCompositeAlpha(capabilities), std::runtime_error);
}
