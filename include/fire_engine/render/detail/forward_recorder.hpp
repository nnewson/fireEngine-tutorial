#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include <vulkan/vulkan.hpp>

#include <fire_engine/render/detail/forward_recording_input.hpp>
#include <fire_engine/render/detail/forward_secondary_recording_worker.hpp>
#include <fire_engine/render/detail/recording_context.hpp>
#include <fire_engine/render/renderer.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Plain attachment handles and compatibility values for one forward pass. */
struct ForwardPassTarget final
{
    vk::Image colorImage{};                          ///< Selected presentable color image.
    vk::ImageView colorView{};                       ///< View used as the color attachment.
    vk::Format colorFormat = vk::Format::eUndefined; ///< Format inherited by forward secondaries.
    vk::Image depthImage{};                          ///< Frame-slot-local depth image.
    vk::ImageView depthView{};                       ///< View used as the depth attachment.
    vk::Format depthFormat = vk::Format::eUndefined; ///< Format inherited by forward secondaries.
    vk::Extent2D extent{};                           ///< Shared attachment extent.
};

/* --- Classes --- */

/** @brief Records one concrete forward pass without renderer or presentation ownership. */
class ForwardRecorder final
{
public:
    /**
     * @brief Creates the fixed forward recording policy and persistent helper.
     * @param mode Direct-primary control or secondary production path.
     * @param forcedParticipantCount Diagnostic override, or unset for workload selection.
     * @throws std::invalid_argument if the forced count is outside the supported range.
     */
    ForwardRecorder(ForwardRecordingMode mode, std::optional<std::size_t> forcedParticipantCount);

    /** @brief Stops the helper before borrowed recording contexts are destroyed. */
    ~ForwardRecorder() noexcept = default;

    ForwardRecorder(const ForwardRecorder&) = delete;
    ForwardRecorder& operator=(const ForwardRecorder&) = delete;
    ForwardRecorder(ForwardRecorder&&) = delete;
    ForwardRecorder& operator=(ForwardRecorder&&) = delete;

    /**
     * @brief Selects the context allocation required by one recording mode.
     * @param mode Fixed forward recording mode.
     * @return Secondary buffer for production, or an empty pool for the direct control.
     */
    [[nodiscard]] static RecordingBufferKind
    secondaryBufferKind(ForwardRecordingMode mode) noexcept;

    /** @brief Returns the fixed recording mode. @return Production or attribution path. */
    [[nodiscard]] ForwardRecordingMode mode() const noexcept;

    /** @brief Returns the diagnostic override. @return Forced count, or no override. */
    [[nodiscard]] std::optional<std::size_t> forcedParticipantCount() const noexcept;

    /** @brief Returns the empirical workload threshold. @return Draws required per participant. */
    [[nodiscard]] std::size_t minimumDrawsPerParticipant() const noexcept;

    /**
     * @brief Records the complete forward rendering instance into an open primary buffer.
     * @param primary Selected frame-slot primary recording context.
     * @param secondaries Selected frame-slot forward participant contexts.
     * @param input Immutable state and packets whose owners outlive this call.
     * @param target Plain selected color and depth attachment handles.
     * @param timings Optional output receiving forward-pass host timings.
     * @pre primary has been reset and its command buffer has begun recording.
     */
    void record(const RecordingContext& primary, std::span<const RecordingContext> secondaries,
                const ForwardRecordingInput& input, const ForwardPassTarget& target,
                ForwardPassCpuTimings* timings);

    /** @brief Reports whether no helper chunk is outstanding. @return Current quiescence. */
    [[nodiscard]] bool idle() const noexcept;

private:
    ForwardRecordingMode mode_; ///< Fixed production or attribution path.
    /// Diagnostic override, or unset when the workload selects the participant count.
    std::optional<std::size_t> forcedParticipantCount_;
    // Declared last so destruction joins the helper before the policy state it reads.
    ForwardSecondaryRecordingWorker helper_; ///< Records the second forward chunk on request.
};
/** @endcond */
} // namespace fire_engine::detail
