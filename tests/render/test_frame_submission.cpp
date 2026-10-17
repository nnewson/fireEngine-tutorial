#include <fire_engine/render/detail/frame_submission.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

// Expected values are independent literals rather than the production stage
// constants used to build the signals.

namespace
{
namespace submission = fire_engine::detail;

// Above 32 bits, so a narrowed serial cannot match.
constexpr std::uint64_t kSerial = 0x1'0000'0003;

template <typename Handle, typename NativeHandle>
[[nodiscard]] Handle fakeHandle(std::uintptr_t value)
{
    if constexpr (std::is_pointer_v<NativeHandle>)
    {
        return Handle{reinterpret_cast<NativeHandle>(value)}; // NOLINT(performance-no-int-to-ptr)
    }
    else
    {
        return Handle{static_cast<NativeHandle>(value)};
    }
}

[[nodiscard]] vk::Semaphore fakeTimeline()
{
    return fakeHandle<vk::Semaphore, VkSemaphore>(0x6000);
}

[[nodiscard]] vk::Semaphore fakeRenderFinished()
{
    return fakeHandle<vk::Semaphore, VkSemaphore>(0x7000);
}

/** @brief Pins one complete signal operation. */
void requireSignal(const vk::SemaphoreSubmitInfo& signal, vk::Semaphore semaphore,
                   std::uint64_t value, vk::PipelineStageFlags2 stageMask)
{
    REQUIRE(signal.sType == vk::StructureType::eSemaphoreSubmitInfo);
    REQUIRE(signal.pNext == nullptr);
    REQUIRE(signal.semaphore == semaphore);
    REQUIRE(signal.value == value);
    REQUIRE(signal.stageMask == stageMask);
    REQUIRE(signal.deviceIndex == 0);
}
} // namespace

TEST_CASE("Ordinary frame submissions signal the timeline and render-finished")
{
    const submission::FrameSignals signals =
        submission::frameSignalSemaphoreInfos(fakeTimeline(), kSerial, fakeRenderFinished(), false);

    REQUIRE(signals.count == 2);
    requireSignal(signals.infos[0], fakeTimeline(), kSerial,
                  vk::PipelineStageFlagBits2::eAllCommands);
    // Binary semaphores ignore the value; the signal leaves it zero.
    requireSignal(signals.infos[1], fakeRenderFinished(), 0,
                  vk::PipelineStageFlagBits2::eColorAttachmentOutput);
}

TEST_CASE("Capture submissions signal only the timeline")
{
    // Capture retires the submission on the host before presenting, so no
    // presentation consumes a render-finished signal.
    const submission::FrameSignals signals =
        submission::frameSignalSemaphoreInfos(fakeTimeline(), kSerial, fakeRenderFinished(), true);

    REQUIRE(signals.count == 1);
    requireSignal(signals.infos[0], fakeTimeline(), kSerial,
                  vk::PipelineStageFlagBits2::eAllCommands);
}

TEST_CASE("The submission retirement stage is pinned exactly")
{
    // All command work, including capture transfers outside the graphics stages.
    REQUIRE(submission::kSubmissionRetirementStage == vk::PipelineStageFlagBits2::eAllCommands);
}
