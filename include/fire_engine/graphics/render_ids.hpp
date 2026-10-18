#pragma once

#include <cstddef>
#include <cstdint>
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

/** @brief Stable index of one decoded CPU image. */
struct ImageId
{
    std::size_t value = std::numeric_limits<std::size_t>::max(); ///< Dense owning-container index.

    /** @brief Returns whether this ID contains an assigned dense index. @return True if valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != std::numeric_limits<std::size_t>::max();
    }

    /** @brief Compares two image IDs by their dense index. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const ImageId&) const noexcept = default;
};

/** @brief Stable index of one image and sampling description. */
struct TextureId
{
    std::size_t value = std::numeric_limits<std::size_t>::max(); ///< Dense owning-container index.

    /** @brief Returns whether this ID contains an assigned dense index. @return True if valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != std::numeric_limits<std::size_t>::max();
    }

    /** @brief Compares two texture IDs by their dense index. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const TextureId&) const noexcept = default;
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

/**
 * @brief Process-unique identity of one RenderAssets content history.
 *
 * The identity follows content: moving a catalog moves its identity, and the
 * moved-from catalog starts a new one. No value is ever issued twice.
 */
struct RenderAssetsId
{
    std::uint64_t value = 0; ///< Issued identity; zero means none.

    /** @brief Returns whether this ID was issued to a catalog. @return True if valid. */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != 0;
    }

    /** @brief Compares two catalog identities. @return True when equal. */
    [[nodiscard]] constexpr bool operator==(const RenderAssetsId&) const noexcept = default;
};

/** @brief Catalog identity and revision that a preparation plan was built from. */
struct RenderAssetsStamp
{
    RenderAssetsId catalog;   ///< Content history the descriptions belong to.
    std::size_t revision = 0; ///< Insertions into that history when stamped.

    /** @brief Compares identity and revision. @return True when both are equal. */
    [[nodiscard]] constexpr bool operator==(const RenderAssetsStamp&) const noexcept = default;
};
} // namespace fire_engine
