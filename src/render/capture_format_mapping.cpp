#include <fire_engine/render/detail/capture_format_mapping.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */

std::optional<CaptureFormat> captureFormatFor(vk::Format format) noexcept
{
    switch (format)
    {
    case vk::Format::eR8G8B8A8Srgb:
        return CaptureFormat::Rgba8Srgb;
    case vk::Format::eB8G8R8A8Srgb:
        return CaptureFormat::Bgra8Srgb;
    default:
        return std::nullopt;
    }
}

/** @endcond */
} // namespace fire_engine::detail
