#include <fire_engine/render/detail/submission_serials.hpp>

#include <cassert>
#include <limits>
#include <stdexcept>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

SubmissionSerials::SubmissionSerials(std::uint64_t initialValue) noexcept
    : lastSubmitted_{initialValue}
{
}

std::size_t SubmissionSerials::slot() const noexcept
{
    return slot_;
}

std::optional<std::uint64_t> SubmissionSerials::retirementValue() const noexcept
{
    return slotSerials_[slot_];
}

std::uint64_t SubmissionSerials::candidate() const
{
    // Reject exhaustion rather than wrap: a wrapped serial would be lower than
    // values already signaled, and a timeline counter never decreases.
    if (lastSubmitted_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Frame submission serials are exhausted");
    }
    return lastSubmitted_ + 1;
}

void SubmissionSerials::commit(std::uint64_t serial) noexcept
{
    assert(lastSubmitted_ != std::numeric_limits<std::uint64_t>::max());
    assert(serial == lastSubmitted_ + 1);
    lastSubmitted_ = serial;
    slotSerials_[slot_] = serial;
    slot_ = (slot_ + 1) % kFrameSlotCount;
}

std::uint64_t SubmissionSerials::lastSubmitted() const noexcept
{
    return lastSubmitted_;
}
/** @endcond */
} // namespace fire_engine::detail
