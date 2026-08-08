#pragma once

namespace fire_engine
{
class RenderAssets;

namespace detail
{
/** @cond INTERNAL */
/**
 * @brief Validates every description and relationship in an asset collection.
 * @param assets Complete Vulkan-free asset catalogue to validate.
 * @throws std::invalid_argument if any mesh, material, or relationship is invalid.
 */
void validateAssets(const RenderAssets& assets);
/** @endcond */
} // namespace detail
} // namespace fire_engine
