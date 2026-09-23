#pragma once

#include <span>

#include <vulkan/vulkan.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Forward declarations --- */

class Device;

/* --- POD structs --- */

/** @brief Optimal-tiling capabilities reported for one shadow-map format candidate. */
struct ShadowMapFormatSupport final
{
    vk::Format format = vk::Format::eUndefined;     ///< Candidate depth format.
    vk::FormatFeatureFlags optimalTilingFeatures{}; ///< Supported optimal-tiling uses.
};

/* --- Functions --- */

/**
 * @brief Selects the preferred format supporting sampled depth-attachment use.
 * @param reportedSupport Capabilities reported for available format candidates.
 * @return D32Sfloat when eligible, otherwise eligible D16Unorm.
 * @throws std::runtime_error if neither registered candidate supports both required uses.
 */
[[nodiscard]] vk::Format
chooseShadowMapFormat(std::span<const ShadowMapFormatSupport> reportedSupport);

/**
 * @brief Queries registered optimal-tiling candidates and applies the tested policy.
 * @param device Physical device whose shadow-depth capabilities are queried.
 * @return Preferred format supporting sampled depth-attachment use.
 * @throws std::runtime_error if neither registered candidate supports both required uses.
 */
[[nodiscard]] vk::Format queryShadowMapFormat(const Device& device);

/**
 * @brief Builds the fixed comparison-sampler policy used for directional shadows.
 *
 * Nearest filtering deliberately exposes the result at the registered shadow-map
 * resolution before a later refinement chooses hardware comparison filtering or
 * shader-defined PCF. Changing to linear filtering is a visual-policy change, not
 * an incidental sampler cleanup.
 *
 * The opaque-white border contributes depth 1.0. With less-or-equal comparison,
 * every valid zero-to-one reference depth compares as lit outside the represented
 * light frustum. The comparison operator and border color therefore form one
 * coupled out-of-frustum policy.
 *
 * @return Nearest, normalized, less-or-equal comparison sampling with a lit border.
 */
[[nodiscard]] constexpr vk::SamplerCreateInfo shadowComparisonSamplerCreateInfo() noexcept
{
    return {
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToBorder,
        .addressModeV = vk::SamplerAddressMode::eClampToBorder,
        .addressModeW = vk::SamplerAddressMode::eClampToBorder,
        .mipLodBias = 0.0F,
        .anisotropyEnable = vk::False,
        .maxAnisotropy = 1.0F,
        .compareEnable = vk::True,
        .compareOp = vk::CompareOp::eLessOrEqual,
        .minLod = 0.0F,
        .maxLod = 0.0F,
        .borderColor = vk::BorderColor::eFloatOpaqueWhite,
        .unnormalizedCoordinates = vk::False,
    };
}
/** @endcond */
} // namespace fire_engine::detail
