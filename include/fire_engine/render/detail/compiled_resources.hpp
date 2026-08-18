#pragma once

#include <cstdint>
#include <memory>

#include <vulkan/vulkan_raii.hpp>

#include <fire_engine/graphics/color4.hpp>
#include <fire_engine/graphics/render_ids.hpp>

namespace fire_engine
{
class RenderAssets;
struct RenderPreparationPlan;

namespace detail
{
class Device;
class FrameInFlight;
class MemoryAllocator;

/* --- POD structs --- */

/** @brief Vulkan handles and material constants required to record one prepared draw. */
struct CompiledDraw
{
    vk::Buffer vertexBuffer;  ///< Device-local vertex buffer.
    vk::Buffer indexBuffer;   ///< Device-local 32-bit index buffer.
    std::uint32_t indexCount; ///< Number of indices consumed by drawIndexed.
    vk::Sampler sampler;      ///< Sampler compiled from the material texture.
    vk::ImageView imageView;  ///< Shader-visible sampled image view.
    Color4 baseColor;         ///< Material factor multiplied by the sampled texture.
};

/* --- Classes --- */

/** @brief Owns the GPU resources compiled from the current render-preparation plan. */
class CompiledResources final
{
public:
    /** @brief Creates an empty compiled-resource collection. */
    CompiledResources();

    /** @brief Releases borrowers before their compiled buffer, image, and sampler owners. */
    ~CompiledResources();

    CompiledResources(const CompiledResources&) = delete;
    CompiledResources& operator=(const CompiledResources&) = delete;
    CompiledResources(CompiledResources&&) = delete;
    CompiledResources& operator=(CompiledResources&&) = delete;

    /**
     * @brief Compiles and atomically replaces the resources selected by one preparation plan.
     * @param device Logical device and graphics queue used for images and transfers.
     * @param allocator VMA owner used for buffers and images.
     * @param frame Quiescent command resources borrowed for setup-time image upload.
     * @param assets Validated Vulkan-free source descriptions.
     * @param plan Referenced subset and render-object lookup produced by preparation.
     */
    void replace(const Device& device, const MemoryAllocator& allocator, FrameInFlight& frame,
                 const RenderAssets& assets, const RenderPreparationPlan& plan);

    /**
     * @brief Reports whether one render object has a complete compiled draw.
     * @param id Render-object ID selected by the current scene.
     * @return True when all referenced GPU resources are available.
     */
    [[nodiscard]] bool contains(RenderObjectId id) const noexcept;

    /**
     * @brief Returns the Vulkan bindings compiled for one render object.
     * @param id Prepared render-object ID.
     * @return Non-owning handles and material constants used to record its draw.
     * @throws std::out_of_range if id is not part of the compiled plan.
     */
    [[nodiscard]] CompiledDraw draw(RenderObjectId id) const;

private:
    class Impl;
    std::unique_ptr<Impl> implementation_; ///< Hidden compiled ownership graph.
};
} // namespace detail
} // namespace fire_engine
