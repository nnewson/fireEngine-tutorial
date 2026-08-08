#pragma once

#include <cstdint>

namespace fire_engine
{
/* --- Enums --- */

/** @brief Reason a vector or quaternion could not be normalized. */
enum class NormalizeError : std::uint8_t
{
    eZeroLength, ///< Every component was zero.
    eNonFinite,  ///< At least one component produced a non-finite length.
};
} // namespace fire_engine
