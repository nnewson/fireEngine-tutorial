#pragma once

#include <cstddef>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Constants --- */

/** @brief Submission slots cycled independently of acquired swapchain images. */
inline constexpr std::size_t kFrameSlotCount = 2;
/** @endcond */
} // namespace fire_engine::detail
