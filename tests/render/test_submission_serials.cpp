#include <fire_engine/render/detail/submission_serials.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

// The renderer commits only after a submission succeeds. A failed acquisition,
// recording or submission is therefore modeled here by calling candidate() and
// not committing.

namespace
{
using fire_engine::detail::kFrameSlotCount;
using fire_engine::detail::SubmissionSerials;

// Some cases spell out the alternation between today's two slots.
static_assert(kFrameSlotCount == 2);

constexpr std::uint64_t kMaxSerial = std::numeric_limits<std::uint64_t>::max();

/** @brief Commits the next serial, as one successful frame submission does. */
std::uint64_t submit(SubmissionSerials& serials)
{
    const std::uint64_t serial = serials.candidate();
    serials.commit(serial);
    return serial;
}
} // namespace

TEST_CASE("Submission serials wait for nothing on each slot's first use")
{
    SubmissionSerials serials;
    for (std::size_t slot = 0; slot < kFrameSlotCount; ++slot)
    {
        REQUIRE(serials.slot() == slot);
        REQUIRE_FALSE(serials.retirementValue().has_value());
        submit(serials);
    }

    // The first slot comes round again and now waits for its first submission.
    REQUIRE(serials.slot() == 0);
    REQUIRE(serials.retirementValue() == std::optional<std::uint64_t>{1});
}

TEST_CASE("Submission serial candidates reserve nothing until committed")
{
    SubmissionSerials serials;
    REQUIRE(serials.candidate() == 1);
    REQUIRE(serials.candidate() == 1);
    REQUIRE(serials.lastSubmitted() == 0);
    REQUIRE(serials.slot() == 0);
    REQUIRE_FALSE(serials.retirementValue().has_value());

    submit(serials);
    REQUIRE(serials.candidate() == 2);
    REQUIRE(serials.candidate() == 2);
    REQUIRE(serials.lastSubmitted() == 1);
    REQUIRE(serials.slot() == 1);
    REQUIRE_FALSE(serials.retirementValue().has_value());
}

TEST_CASE("Committing a submission publishes its serial and advances the slot")
{
    SubmissionSerials serials;
    serials.commit(1);
    REQUIRE(serials.lastSubmitted() == 1);
    REQUIRE(serials.slot() == 1);

    serials.commit(2);
    REQUIRE(serials.lastSubmitted() == 2);
    REQUIRE(serials.slot() == 0);
    REQUIRE(serials.retirementValue() == std::optional<std::uint64_t>{1});
}

TEST_CASE("Sustained submission retires each slot by its own last serial")
{
    SubmissionSerials serials;
    std::array<std::optional<std::uint64_t>, kFrameSlotCount> expected{};
    for (std::uint64_t serial = 1; serial <= 4 * kFrameSlotCount + 1; ++serial)
    {
        const std::size_t slot = serials.slot();
        REQUIRE(slot == (serial - 1) % kFrameSlotCount);
        REQUIRE(serials.retirementValue() == expected[slot]);
        REQUIRE(serials.candidate() == serial);
        serials.commit(serial);
        REQUIRE(serials.lastSubmitted() == serial);
        expected[slot] = serial;
    }
}

TEST_CASE("An initial serial offsets candidates without implying earlier submissions")
{
    SubmissionSerials serials{41};
    REQUIRE(serials.lastSubmitted() == 41);
    REQUIRE(serials.candidate() == 42);
    REQUIRE(serials.slot() == 0);
    REQUIRE_FALSE(serials.retirementValue().has_value());
}

TEST_CASE("Exhausted submission serials reject the next candidate without change")
{
    SubmissionSerials serials{kMaxSerial};
    REQUIRE_THROWS_AS(serials.candidate(), std::overflow_error);
    REQUIRE(serials.lastSubmitted() == kMaxSerial);
    REQUIRE(serials.slot() == 0);
    REQUIRE_FALSE(serials.retirementValue().has_value());
}

TEST_CASE("Committing the final serial exhausts the next candidate")
{
    SubmissionSerials serials{kMaxSerial - 1};
    REQUIRE(submit(serials) == kMaxSerial);
    REQUIRE(serials.lastSubmitted() == kMaxSerial);
    REQUIRE(serials.slot() == 1);

    REQUIRE_THROWS_AS(serials.candidate(), std::overflow_error);
    REQUIRE(serials.lastSubmitted() == kMaxSerial);
    REQUIRE(serials.slot() == 1);
    REQUIRE_FALSE(serials.retirementValue().has_value());
}
