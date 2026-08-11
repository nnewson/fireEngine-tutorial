#pragma once

#include <filesystem>

#include <fire_engine/content/scene_content.hpp>

namespace fire_engine
{
/* --- Classes --- */

/** @brief Imports the deliberately small glTF 2.0 subset used by the tutorial. */
class GltfLoader final
{
public:
    /**
     * @brief Loads one JSON glTF and its external buffers and PNG images.
     * @param path Path to the `.gltf` file; referenced files resolve relative to it.
     * @return Validated Vulkan-free scene content ready for renderer preparation.
     * @throws std::runtime_error when the file is malformed or uses unsupported required data.
     */
    [[nodiscard]] SceneContent load(const std::filesystem::path& path) const;
};
} // namespace fire_engine
