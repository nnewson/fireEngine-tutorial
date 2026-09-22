#include <fire_engine/render/detail/shadow_map_policy.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <span>

namespace
{
namespace policy = fire_engine::detail;

constexpr vk::FormatFeatureFlags kAttachment = vk::FormatFeatureFlagBits::eDepthStencilAttachment;
constexpr vk::FormatFeatureFlags kSampled = vk::FormatFeatureFlagBits::eSampledImage;
constexpr vk::FormatFeatureFlags kRequired = kAttachment | kSampled;

[[nodiscard]] constexpr policy::ShadowMapFormatSupport
support(vk::Format format, vk::FormatFeatureFlags features) noexcept
{
    return {
        .format = format,
        .optimalTilingFeatures = features,
    };
}
} // namespace

TEST_CASE("Shadow-map format selection owns its preference order")
{
    const std::array preferredFirst = {
        support(vk::Format::eD32Sfloat, kRequired),
        support(vk::Format::eD16Unorm, kRequired),
    };
    const std::array fallbackFirst = {
        support(vk::Format::eD16Unorm, kRequired),
        support(vk::Format::eD32Sfloat, kRequired),
    };

    REQUIRE(policy::chooseShadowMapFormat(preferredFirst) == vk::Format::eD32Sfloat);
    REQUIRE(policy::chooseShadowMapFormat(fallbackFirst) == vk::Format::eD32Sfloat);
}

TEST_CASE("Shadow-map format selection requires sampled depth-attachment support")
{
    SECTION("D32 missing sampled-image support falls back")
    {
        const std::array reported = {
            support(vk::Format::eD32Sfloat, kAttachment),
            support(vk::Format::eD16Unorm, kRequired),
        };
        REQUIRE(policy::chooseShadowMapFormat(reported) == vk::Format::eD16Unorm);
    }

    SECTION("D32 missing attachment support falls back")
    {
        const std::array reported = {
            support(vk::Format::eD32Sfloat, kSampled),
            support(vk::Format::eD16Unorm, kRequired),
        };
        REQUIRE(policy::chooseShadowMapFormat(reported) == vk::Format::eD16Unorm);
    }

    SECTION("Unrelated features do not change eligibility")
    {
        const std::array reported = {
            support(vk::Format::eD32Sfloat, kRequired | vk::FormatFeatureFlagBits::eStorageImage),
        };
        REQUIRE(policy::chooseShadowMapFormat(reported) == vk::Format::eD32Sfloat);
    }
}

TEST_CASE("Shadow-map format selection rejects incomplete support")
{
    const std::array attachmentOnly = {
        support(vk::Format::eD32Sfloat, kAttachment),
        support(vk::Format::eD16Unorm, kAttachment),
    };
    const std::array sampledOnly = {
        support(vk::Format::eD32Sfloat, kSampled),
        support(vk::Format::eD16Unorm, kSampled),
    };
    const std::span<const policy::ShadowMapFormatSupport> empty;

    REQUIRE_THROWS_WITH(policy::chooseShadowMapFormat(attachmentOnly),
                        Catch::Matchers::ContainsSubstring("no sampled depth-attachment format"));
    REQUIRE_THROWS_WITH(policy::chooseShadowMapFormat(sampledOnly),
                        Catch::Matchers::ContainsSubstring("no sampled depth-attachment format"));
    REQUIRE_THROWS_WITH(policy::chooseShadowMapFormat(empty),
                        Catch::Matchers::ContainsSubstring("no sampled depth-attachment format"));
}

TEST_CASE("Shadow comparison sampler pins filtering, comparison, and border policy")
{
    constexpr vk::SamplerCreateInfo sampler = policy::shadowComparisonSamplerCreateInfo();

    REQUIRE(sampler.flags == vk::SamplerCreateFlags{});
    REQUIRE(sampler.magFilter == vk::Filter::eNearest);
    REQUIRE(sampler.minFilter == vk::Filter::eNearest);
    REQUIRE(sampler.mipmapMode == vk::SamplerMipmapMode::eNearest);
    REQUIRE(sampler.addressModeU == vk::SamplerAddressMode::eClampToBorder);
    REQUIRE(sampler.addressModeV == vk::SamplerAddressMode::eClampToBorder);
    REQUIRE(sampler.addressModeW == vk::SamplerAddressMode::eClampToBorder);
    REQUIRE(sampler.mipLodBias == 0.0F);
    REQUIRE(sampler.anisotropyEnable == vk::False);
    REQUIRE(sampler.maxAnisotropy == 1.0F);
    REQUIRE(sampler.compareEnable == vk::True);
    REQUIRE(sampler.compareOp == vk::CompareOp::eLessOrEqual);
    REQUIRE(sampler.minLod == 0.0F);
    REQUIRE(sampler.maxLod == 0.0F);
    REQUIRE(sampler.borderColor == vk::BorderColor::eFloatOpaqueWhite);
    REQUIRE(sampler.unnormalizedCoordinates == vk::False);
}
