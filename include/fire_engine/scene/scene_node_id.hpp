#pragma once

#include <cstddef>
#include <limits>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Stable typed reference to a node registered with one Scene. */
struct SceneNodeId
{
    std::size_t value = std::numeric_limits<std::size_t>::max(); ///< Dense scene-local index.

    /** @brief Reports whether this ID has been assigned by a Scene. @return True when valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != std::numeric_limits<std::size_t>::max();
    }

    /** @brief Compares the stored scene-local indices. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const SceneNodeId&) const noexcept = default;
};
} // namespace fire_engine
