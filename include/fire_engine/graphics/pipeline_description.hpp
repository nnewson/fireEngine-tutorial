#pragma once

#include <cstdint>

namespace fire_engine
{
/* --- Enums --- */

/** @brief Vulkan-free identity of one interleaved vertex-buffer layout. */
enum class VertexLayoutKey : std::uint8_t
{
    ePositionColorTextureCoordinate, ///< Position, linear color, then texture coordinate.
};

/* --- POD structs --- */

/** @brief Vulkan-free resource compatibility requirements for one graphics pipeline. */
struct PipelineDescription
{
    VertexLayoutKey vertexLayout =
        VertexLayoutKey::ePositionColorTextureCoordinate; ///< Required mesh layout.

    /**
     * @brief Compares every Vulkan-free compatibility requirement.
     * @param other Pipeline description to compare.
     * @return True when both descriptions require the same resource layouts.
     */
    [[nodiscard]] bool operator==(const PipelineDescription& other) const noexcept = default;
};
} // namespace fire_engine
