#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <vector>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Enums --- */

/** @brief Supported byte layouts for an sRGB frame capture. */
enum class CaptureFormat : std::uint8_t
{
    Rgba8Srgb, ///< Red occupies the first byte of each pixel.
    Bgra8Srgb, ///< Blue occupies the first byte of each pixel.
};

/* --- Functions --- */

/**
 * @brief Computes the byte count occupied by rows with an explicit pitch.
 * @param height Number of rows.
 * @param rowPitch Bytes occupied by each row, including any padding.
 * @return Required byte count, saturated at the largest `std::size_t` when the
 * product is not representable.
 */
[[nodiscard]] constexpr std::size_t captureByteSize(std::size_t height,
                                                    std::size_t rowPitch) noexcept
{
    if (rowPitch != 0 && height > std::numeric_limits<std::size_t>::max() / rowPitch)
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return height * rowPitch;
}

/**
 * @brief Converts captured sRGB pixels into tightly packed RGBA8 rows.
 *
 * Source row padding is skipped and destination alpha is forced to opaque.
 * Invalid dimensions, pitches, mappings, or formats return an empty vector.
 *
 * @param mapped Source bytes containing the captured rows.
 * @param width Pixel width.
 * @param height Pixel height.
 * @param rowPitch Source bytes per row, including any padding.
 * @param format Source channel order and transfer function.
 * @return Tightly packed RGBA8 pixels, or an empty vector on invalid input.
 */
[[nodiscard]] std::vector<std::uint8_t> toRgba8(std::span<const std::byte> mapped,
                                                std::size_t width, std::size_t height,
                                                std::size_t rowPitch, CaptureFormat format);

/**
 * @brief Writes tightly packed RGBA8 pixels to a PNG file.
 * @param path Destination file path.
 * @param rgba Source pixels in row-major RGBA8 order.
 * @param width Pixel width.
 * @param height Pixel height.
 * @return True when stb opened the destination and reported successful
 * encoding. The result cannot detect every underlying stdio short write.
 */
[[nodiscard]] bool writeRgba8Png(const std::filesystem::path& path,
                                 std::span<const std::uint8_t> rgba, std::size_t width,
                                 std::size_t height);
/** @endcond */
} // namespace fire_engine::detail
