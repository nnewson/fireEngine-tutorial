#pragma once

#include <cstddef>
#include <limits>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Stable index of one reusable animation description. */
struct AnimationId
{
    std::size_t value = std::numeric_limits<std::size_t>::max(); ///< Owning-container index.

    /** @brief Returns whether this ID contains an assigned dense index. @return True if valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != std::numeric_limits<std::size_t>::max();
    }

    /** @brief Compares two animation IDs by their dense index. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const AnimationId&) const noexcept = default;
};

/** @brief Stable index of one channel inside an Animation. */
struct AnimationChannelId
{
    std::size_t value = std::numeric_limits<std::size_t>::max(); ///< Animation-local index.

    /** @brief Returns whether this ID contains an assigned dense index. @return True if valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != std::numeric_limits<std::size_t>::max();
    }

    /** @brief Compares two channel IDs by their dense index. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const AnimationChannelId&) const noexcept = default;
};
} // namespace fire_engine
