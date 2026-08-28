#pragma once

#include <memory>

#include <vulkan/vulkan_raii.hpp>

namespace fire_engine
{
class RenderAssets;
struct RenderPreparationPlan;

namespace detail
{
struct CompiledResourceGraph;
class Device;
class MemoryAllocator;

/** @cond INTERNAL */
/* --- Classes --- */

/**
 * @brief Builds complete GPU resource graphs with a dedicated upload context.
 *
 * Compilation owns its transient command pool, command buffer, fence, and
 * staging allocations. Successful compilation returns a complete graph for
 * atomic transfer into CompiledResources; the renderer's submission frame is
 * never borrowed for setup work.
 */
class ResourceCompiler final
{
public:
    /**
     * @brief Creates the upload context retained across compilation passes.
     * @param device Logical device, graphics family, and queue used for uploads.
     * @param allocator VMA owner used for staging and compiled allocations.
     * @throws vk::SystemError if the upload pool, command buffer, or fence cannot be created.
     */
    ResourceCompiler(const Device& device, const MemoryAllocator& allocator);

    /** @brief Releases the upload fence, command buffer, and command pool. */
    ~ResourceCompiler() = default;

    /// @brief Copy construction is disabled because Vulkan handles have unique ownership.
    ResourceCompiler(const ResourceCompiler&) = delete;
    /// @brief Copy assignment is disabled because Vulkan handles have unique ownership.
    ResourceCompiler& operator=(const ResourceCompiler&) = delete;
    /// @brief Move construction is disabled so borrowed device owners remain explicit.
    ResourceCompiler(ResourceCompiler&&) = delete;
    /// @brief Move assignment is disabled so borrowed device owners remain explicit.
    ResourceCompiler& operator=(ResourceCompiler&&) = delete;

    /**
     * @brief Compiles one validated plan into a complete candidate graph.
     * @param assets Vulkan-free sources retained for the duration of this call.
     * @param plan Prepared subset with proved pipeline compatibility.
     * @param previous Stable graph supplying any safely reusable immutable resources.
     * @return Complete candidate graph ready for an atomic ownership replacement.
     * @throws std::runtime_error if allocation or upload fails.
     * @throws vk::SystemError if recording, submission, or waiting fails.
     */
    [[nodiscard]] std::unique_ptr<CompiledResourceGraph>
    compile(const RenderAssets& assets, const RenderPreparationPlan& plan,
            const CompiledResourceGraph& previous);

private:
    const Device* device_ = nullptr;                   ///< Borrowed long-lived Vulkan device.
    const MemoryAllocator* allocator_ = nullptr;       ///< Borrowed long-lived VMA owner.
    vk::raii::CommandPool commandPool_{nullptr};       ///< Transient upload command arena.
    vk::raii::CommandBuffers commandBuffers_{nullptr}; ///< Contains one primary upload buffer.
    vk::raii::Fence uploadFinished_{nullptr};          ///< Signals upload submission completion.
};
/** @endcond */
} // namespace detail
} // namespace fire_engine
