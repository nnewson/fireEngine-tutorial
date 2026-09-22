#include <fire_engine/render/detail/shadow_map_policy.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace fire_engine::detail
{
/** @cond INTERNAL */
namespace
{
/* --- Constants --- */

/** @brief Registered shadow-depth formats in descending preference order. */
constexpr std::array kPreferredFormats = {
    vk::Format::eD32Sfloat,
    vk::Format::eD16Unorm,
};

/**
 * @brief Optimal-tiling capabilities required by every shadow-map format.
 *
 * Keep this policy distinct from DepthBuffer's attachment-only selection. A
 * device may support D32 as an attachment but only D16 for sampled depth, so
 * combining the selectors could silently select an unusable shadow format.
 */
constexpr vk::FormatFeatureFlags kRequiredFeatures =
    vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage;

/* --- File-local functions --- */

/**
 * @brief Returns whether the reported format supports every shadow-map use.
 * @param support Format and optimal-tiling capabilities to inspect.
 * @return True when attachment and sampled-image support are both present.
 */
[[nodiscard]] constexpr bool isEligible(const ShadowMapFormatSupport& support) noexcept
{
    return (support.optimalTilingFeatures & kRequiredFeatures) == kRequiredFeatures;
}
} // namespace

/* --- Internal functions --- */

vk::Format chooseShadowMapFormat(std::span<const ShadowMapFormatSupport> reportedSupport)
{
    for (const vk::Format preferredFormat : kPreferredFormats)
    {
        const auto candidate =
            std::ranges::find(reportedSupport, preferredFormat, &ShadowMapFormatSupport::format);
        if (candidate != reportedSupport.end() && isEligible(*candidate))
        {
            return preferredFormat;
        }
    }
    throw std::runtime_error("The selected device supports no sampled depth-attachment format");
}
/** @endcond */
} // namespace fire_engine::detail
