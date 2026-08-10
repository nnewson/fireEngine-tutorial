#pragma once

#include <cstdint>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Constants --- */

/**
 * @brief Golden-ratio constant for hash-combine-style mixing of a 32-bit word.
 */
inline constexpr std::uint32_t k32BitGoldenRatio = 0x9e3779b9U;

/**
 * @brief Golden-ratio constant for hash-combine-style mixing of a 64-bit word.
 */
inline constexpr std::uint64_t k64BitGoldenRatio = 0x9e3779b97f4a7c15ULL;
/** @endcond */
} // namespace fire_engine::detail
