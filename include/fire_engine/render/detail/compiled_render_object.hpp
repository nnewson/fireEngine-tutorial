#pragma once

#include <cstdint>

#include <vulkan/vulkan.hpp>

#include <fire_engine/graphics/color4.hpp>
#include <fire_engine/graphics/pipeline_description.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Plain Vulkan geometry handles and their retained vertex-layout proof. */
struct CompiledGeometry
{
    vk::Buffer vertexBuffer;      ///< Device-local vertex buffer.
    vk::Buffer indexBuffer;       ///< Device-local 32-bit index buffer.
    std::uint32_t indexCount;     ///< Number of indices consumed by drawIndexed.
    VertexLayoutKey vertexLayout; ///< Mesh/pipeline compatibility proved before compilation.
};

/** @brief Plain handles and constants used only by forward material recording. */
struct CompiledForwardMaterial
{
    vk::Sampler sampler;     ///< Sampler compiled from the base-color texture.
    vk::ImageView imageView; ///< Shader-visible base-color image view.
    Color4 baseColor;        ///< Factor multiplied by the sampled texture.
};

/** @brief Complete compiled relationships for one render object. */
struct CompiledRenderObject
{
    CompiledGeometry geometry;               ///< Pass-independent indexed geometry.
    CompiledForwardMaterial forwardMaterial; ///< Forward-only sampled material state.
};
/** @endcond */
} // namespace fire_engine::detail
