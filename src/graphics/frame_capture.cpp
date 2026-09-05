#include <fire_engine/graphics/detail/frame_capture.hpp>

#include <limits>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace fire_engine::detail
{
namespace
{
/* --- Constants --- */

/** @brief Number of bytes in one supported source or destination pixel. */
constexpr std::size_t kChannelCount = 4;

/* --- Functions --- */

/**
 * @brief Checks whether multiplying two sizes is representable.
 * @param left Left operand.
 * @param right Right operand.
 * @return True when the product fits in `std::size_t`.
 */
[[nodiscard]] constexpr bool multiplicationFits(std::size_t left, std::size_t right) noexcept
{
    return right == 0 || left <= std::numeric_limits<std::size_t>::max() / right;
}
} // namespace

std::vector<std::uint8_t> toRgba8(std::span<const std::byte> mapped, std::size_t width,
                                  std::size_t height, std::size_t rowPitch, CaptureFormat format)
{
    if (width == 0 || height == 0 || !multiplicationFits(width, kChannelCount))
    {
        return {};
    }
    const std::size_t packedRowSize = width * kChannelCount;
    if (rowPitch < packedRowSize || !multiplicationFits(height, rowPitch) ||
        mapped.size() < captureByteSize(height, rowPitch) ||
        !multiplicationFits(height, packedRowSize))
    {
        return {};
    }

    std::size_t redIndex = 0;
    std::size_t blueIndex = 0;
    switch (format)
    {
    case CaptureFormat::Rgba8Srgb:
        redIndex = 0;
        blueIndex = 2;
        break;
    case CaptureFormat::Bgra8Srgb:
        redIndex = 2;
        blueIndex = 0;
        break;
    default:
        return {};
    }

    std::vector<std::uint8_t> rgba(height * packedRowSize);
    for (std::size_t row = 0; row < height; ++row)
    {
        const std::byte* source = mapped.data() + row * rowPitch;
        std::uint8_t* destination = rgba.data() + row * packedRowSize;
        for (std::size_t column = 0; column < width; ++column)
        {
            const std::byte* pixel = source + column * kChannelCount;
            destination[column * kChannelCount] = std::to_integer<std::uint8_t>(pixel[redIndex]);
            destination[column * kChannelCount + 1] = std::to_integer<std::uint8_t>(pixel[1]);
            destination[column * kChannelCount + 2] =
                std::to_integer<std::uint8_t>(pixel[blueIndex]);
            destination[column * kChannelCount + 3] = 0xFF;
        }
    }
    return rgba;
}

bool writeRgba8Png(const std::filesystem::path& path, std::span<const std::uint8_t> rgba,
                   std::size_t width, std::size_t height)
{
    constexpr std::size_t kMaximumWidth =
        static_cast<std::size_t>(std::numeric_limits<int>::max()) / kChannelCount;
    // The multiplication guard is redundant with the int limits on 64-bit
    // targets but remains necessary when std::size_t is 32-bit.
    if (path.empty() || width == 0 || height == 0 || width > kMaximumWidth ||
        height > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !multiplicationFits(width * kChannelCount, height))
    {
        return false;
    }
    const std::size_t requiredByteCount = width * height * kChannelCount;
    if (rgba.size() < requiredByteCount)
    {
        return false;
    }

    const std::string pathString = path.string();
    return stbi_write_png(pathString.c_str(), static_cast<int>(width), static_cast<int>(height),
                          static_cast<int>(kChannelCount), rgba.data(),
                          static_cast<int>(width * kChannelCount)) != 0;
}
} // namespace fire_engine::detail
