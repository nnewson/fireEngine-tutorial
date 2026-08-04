#pragma once

#include <cstdint>

#include <fire_engine/render/buffer.hpp>
#include <fire_engine/render/frame_in_flight.hpp>

namespace fire_engine
{
/* --- Forward declarations --- */

class Device;
class MemoryAllocator;
class Pipeline;
class Swapchain;

/* --- Enums --- */

/** @brief Outcome of one attempt to render and present a frame. */
enum class RenderResult : std::uint8_t
{
    ePresented,           ///< Presented one image; the current swapchain remains suitable.
    ePresentedSuboptimal, ///< Presented one image, but the swapchain should be replaced.
    eNotPresented,        ///< Presented nothing because the swapchain is out of date.
};

/* --- Classes --- */

/**
 * @brief Records, submits, and presents the tutorial's single triangle.
 *
 * The application owns its event loop. This class owns the resources used by
 * one frame in flight and hides the Vulkan operation sequence performed during
 * each iteration of that loop. Device, allocator, swapchain, and pipeline are
 * borrowed and must all outlive this object.
 */
class Renderer final
{
public:
    /**
     * @brief Creates and uploads the triangle and initializes one frame slot.
     * @param device Logical device and queues used to execute and present work.
     * @param allocator VMA owner used for vertex and uniform buffers.
     * @param swapchain Presentable images targeted by the pipeline.
     * @param pipeline Dynamic-rendering graphics pipeline used for the draw.
     * @throws std::runtime_error if a buffer cannot be created or populated.
     * @throws vk::SystemError if a frame resource cannot be created.
     */
    Renderer(const Device& device, const MemoryAllocator& allocator, const Swapchain& swapchain,
             const Pipeline& pipeline);

    /**
     * @brief Waits without throwing if submitted work may remain during unwinding.
     *
     * Normal shutdown should call waitIdle() so an error can be reported. If an
     * exception leaves the event loop, this fallback waits before Renderer-owned
     * buffers and frame resources are destroyed. Presentation-resource lifetime
     * remains the separate precondition documented by Swapchain.
     */
    ~Renderer() noexcept;

    /// @brief Copy construction is disabled because render resources have one owner.
    Renderer(const Renderer&) = delete;
    /// @brief Copy assignment is disabled because render resources have one owner.
    Renderer& operator=(const Renderer&) = delete;
    /// @brief Move construction is disabled because this object borrows stable owners.
    Renderer(Renderer&&) = delete;
    /// @brief Move assignment is disabled because this object borrows stable owners.
    Renderer& operator=(Renderer&&) = delete;

    /**
     * @brief Acquires, records, submits, and presents one swapchain image.
     * @return Whether an image was presented and whether the swapchain remains suitable.
     * @throws vk::SystemError if an unexpected Vulkan operation fails.
     */
    [[nodiscard]] RenderResult renderFrame();

    /**
     * @brief Waits for all device work before dependent render resources are destroyed.
     * @throws vk::SystemError if the device reports that the wait failed.
     */
    void waitIdle();

private:
    /**
     * @brief Records the dynamic-rendering commands for one acquired image.
     * @param imageIndex Index returned by swapchain image acquisition.
     */
    void recordCommands(std::uint32_t imageIndex) const;

    const Device& device_;       ///< Device and queues borrowed by every frame.
    const Swapchain& swapchain_; ///< Images and per-image synchronization borrowed by every frame.
    const Pipeline& pipeline_;   ///< Pipeline and layout borrowed by command recording.
    AllocatedBuffer vertexBuffer_;  ///< Immutable triangle vertices owned by this renderer.
    FrameInFlight frame_;           ///< Command and per-frame synchronization state.
    bool workMayBePending_ = false; ///< Whether destruction needs a defensive device wait.
};
} // namespace fire_engine
