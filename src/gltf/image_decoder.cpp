#include <fire_engine/gltf/detail/image_decoder.hpp>

#include <limits>
#include <stdexcept>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Functions --- */

ImageData decodeRgba8(const std::filesystem::path& path)
{
    int width = 0;
    int height = 0;
    int sourceChannelCount = 0;
    constexpr int kRgbaChannelCount = 4;
    stbi_uc* decoded =
        stbi_load(path.string().c_str(), &width, &height, &sourceChannelCount, kRgbaChannelCount);
    if (decoded == nullptr)
    {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error("Could not decode image '" + path.string() +
                                 "': " + (reason == nullptr ? "unknown stb error" : reason));
    }

    if (width <= 0 || height <= 0 ||
        static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() /
                                              static_cast<std::size_t>(height) / kRgbaChannelCount)
    {
        stbi_image_free(decoded);
        throw std::runtime_error("Decoded image has invalid dimensions: " + path.string());
    }

    const std::size_t byteCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kRgbaChannelCount;
    ImageData result{
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .pixels = std::vector<std::uint8_t>(decoded, decoded + byteCount),
    };
    stbi_image_free(decoded);
    return result;
}
/** @endcond */
} // namespace fire_engine::detail
