#pragma once

#include <cstddef>
#include <limits>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Stable index of one CPU mesh description. */
struct MeshId
{
    std::size_t value = std::numeric_limits<std::size_t>::max(); ///< Dense owning-container index.

    /** @brief Returns whether this ID contains an assigned dense index. @return True if valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != std::numeric_limits<std::size_t>::max();
    }

    /** @brief Compares two mesh IDs by their dense index. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const MeshId&) const noexcept = default;
};

/** @brief Stable index of one CPU material description. */
struct MaterialId
{
    std::size_t value = std::numeric_limits<std::size_t>::max(); ///< Dense owning-container index.

    /** @brief Returns whether this ID contains an assigned dense index. @return True if valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != std::numeric_limits<std::size_t>::max();
    }

    /** @brief Compares two material IDs by their dense index. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const MaterialId&) const noexcept = default;
};

/** @brief Stable index of one mesh/material relationship. */
struct RenderObjectId
{
    std::size_t value = std::numeric_limits<std::size_t>::max(); ///< Dense owning-container index.

    /** @brief Returns whether this ID contains an assigned dense index. @return True if valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != std::numeric_limits<std::size_t>::max();
    }

    /** @brief Compares two render-object IDs by their dense index. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const RenderObjectId&) const noexcept = default;
};
} // namespace fire_engine
