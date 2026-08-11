#pragma once

#include <filesystem>

#include <fire_engine/graphics/image_data.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Functions --- */

/**
 * @brief Decodes one external image as tightly packed RGBA8 pixels.
 * @param path Image file to decode.
 * @return Owned decoded pixels.
 * @throws std::runtime_error when stb cannot decode the file.
 */
[[nodiscard]] ImageData decodeRgba8(const std::filesystem::path& path);
/** @endcond */
} // namespace fire_engine::detail
