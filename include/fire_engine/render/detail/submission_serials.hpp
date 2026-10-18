#pragma once

#include <fire_engine/render/detail/frame_slot_count.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Classes --- */

/**
 * @brief Tracks frame submission serials and the frame slot selected for reuse.
 *
 * Each successful frame submission signals the next serial on the renderer's
 * timeline semaphore. A slot may be reused only after the serial of its own last
 * successful submission completes; slot order alone is not the contract.
 *
 * Nothing is published until commit(), which the renderer calls only after
 * submission succeeds. An attempt that fails before then, during acquisition,
 * recording or submission, therefore consumes no serial and leaves the selected
 * slot unchanged.
 */
class SubmissionSerials final
{
public:
    /**
     * @brief Starts with no submissions and the first slot selected.
     * @param initialValue Counter value the timeline semaphore is created with.
     */
    explicit SubmissionSerials(std::uint64_t initialValue = 0) noexcept;

    /** @brief Returns the slot selected for the next attempt. @return Slot index. */
    [[nodiscard]] std::size_t slot() const noexcept;

    /**
     * @brief Returns the serial that must complete before the selected slot is reused.
     * @return The slot's last successfully submitted serial, or none on its first use.
     */
    [[nodiscard]] std::optional<std::uint64_t> retirementValue() const noexcept;

    /**
     * @brief Returns the serial the next submission signals, without reserving it.
     * @return One more than lastSubmitted().
     * @throws std::overflow_error if every serial has been used.
     */
    [[nodiscard]] std::uint64_t candidate() const;

    /**
     * @brief Publishes one successful submission and selects the next slot.
     * @param serial Serial the submission signals; it must equal candidate().
     */
    void commit(std::uint64_t serial) noexcept;

    /** @brief Returns the last published serial. @return Initial value before any commit. */
    [[nodiscard]] std::uint64_t lastSubmitted() const noexcept;

private:
    std::uint64_t lastSubmitted_; ///< Highest serial published by a successful submission.
    std::array<std::optional<std::uint64_t>, kFrameSlotCount>
        slotSerials_{};    ///< Each slot's last successfully submitted serial, if any.
    std::size_t slot_ = 0; ///< Slot selected for the next attempt.
};
/** @endcond */
} // namespace fire_engine::detail
