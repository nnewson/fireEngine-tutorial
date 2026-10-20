#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Functions --- */

/**
 * @brief Issues the next catalog identity from a counter, failing closed at exhaustion.
 *
 * The counter holds the next value to issue. Issuing the largest value wraps
 * the counter to zero, which then means every value has been issued, so no
 * value is ever returned twice and zero is never returned.
 *
 * @param counter Next identity to issue, or zero once exhausted.
 * @return The issued nonzero identity, or nullopt when the counter is exhausted.
 */
[[nodiscard]] std::optional<std::uint64_t>
issueRenderAssetsId(std::atomic<std::uint64_t>& counter) noexcept;
/** @endcond */
} // namespace fire_engine::detail
