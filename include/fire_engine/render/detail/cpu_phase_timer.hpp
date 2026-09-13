#pragma once

#include <chrono>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Classes --- */

/** @brief Accumulates one optional host phase without adding owner state. */
class CpuPhaseTimer final
{
public:
    /** @brief Starts timing when output is non-null. @param output Optional phase accumulator. */
    explicit CpuPhaseTimer(std::chrono::nanoseconds* output) noexcept
        : output_{output}
    {
        if (output_ != nullptr)
        {
            start_ = std::chrono::steady_clock::now();
        }
    }

    /** @brief Adds elapsed host time to the supplied accumulator. */
    ~CpuPhaseTimer() noexcept
    {
        if (output_ != nullptr)
        {
            *output_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start_);
        }
    }

    CpuPhaseTimer(const CpuPhaseTimer&) = delete;
    CpuPhaseTimer& operator=(const CpuPhaseTimer&) = delete;
    CpuPhaseTimer(CpuPhaseTimer&&) = delete;
    CpuPhaseTimer& operator=(CpuPhaseTimer&&) = delete;

private:
    std::chrono::nanoseconds* output_ = nullptr;    ///< Optional duration receiving elapsed time.
    std::chrono::steady_clock::time_point start_{}; ///< Start sampled only when output exists.
};
/** @endcond */
} // namespace fire_engine::detail
