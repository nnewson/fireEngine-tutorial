#pragma once

#include <cstdint>

#include <vulkan/vulkan.hpp>

#include <fire_engine/graphics/color4.hpp>
#include <fire_engine/graphics/pipeline_description.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- POD structs --- */

/** @brief Plain Vulkan handles and constants compiled for one render object. */
struct CompiledDraw
{
    vk::Buffer vertexBuffer;      ///< Device-local vertex buffer.
    vk::Buffer indexBuffer;       ///< Device-local 32-bit index buffer.
    std::uint32_t indexCount;     ///< Number of indices consumed by drawIndexed.
    vk::Sampler sampler;          ///< Sampler compiled from the material texture.
    vk::ImageView imageView;      ///< Shader-visible sampled image view.
    Color4 baseColor;             ///< Material factor multiplied by the sampled texture.
    VertexLayoutKey vertexLayout; ///< Mesh/pipeline compatibility proved before compilation.
};
/** @endcond */
} // namespace fire_engine::detail
