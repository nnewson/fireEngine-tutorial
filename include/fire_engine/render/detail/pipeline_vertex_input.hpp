#pragma once

#include <vulkan/vulkan.hpp>

#include <fire_engine/graphics/pipeline_description.hpp>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Functions --- */

/**
 * @brief Maps one Vulkan-free layout key to its shared interleaved buffer binding.
 * @param vertexLayout Layout required by a concrete graphics pipeline.
 * @return Binding whose stride is shared by every pass consuming that layout.
 * @throws std::invalid_argument if the key has no Vulkan mapping.
 */
[[nodiscard]] const vk::VertexInputBindingDescription&
compileVertexBinding(VertexLayoutKey vertexLayout);
/** @endcond */
} // namespace fire_engine::detail
