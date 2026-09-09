#include <fire_engine/graphics/detail/frame_capture.hpp>
#include <fire_engine/render/detail/capture_format_mapping.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Capture format mapping accepts only supported sRGB byte layouts")
{
    using fire_engine::detail::CaptureFormat;
    using fire_engine::detail::captureFormatFor;

    REQUIRE(captureFormatFor(vk::Format::eR8G8B8A8Srgb) == CaptureFormat::Rgba8Srgb);
    REQUIRE(captureFormatFor(vk::Format::eB8G8R8A8Srgb) == CaptureFormat::Bgra8Srgb);
    REQUIRE_FALSE(captureFormatFor(vk::Format::eR8G8B8A8Unorm).has_value());
    REQUIRE_FALSE(captureFormatFor(vk::Format::eB8G8R8A8Unorm).has_value());
    REQUIRE_FALSE(captureFormatFor(vk::Format::eUndefined).has_value());
}
