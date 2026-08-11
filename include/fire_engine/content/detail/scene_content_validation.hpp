#pragma once

namespace fire_engine
{
struct SceneContent;

namespace detail
{
/** @cond INTERNAL */
/**
 * @brief Validates every relationship spanning one format-neutral scene composition.
 * @param content Assets, hierarchy, and animation bindings to validate together.
 * @throws std::invalid_argument if any description or cross-domain reference is invalid.
 */
void validateSceneContent(const SceneContent& content);
/** @endcond */
} // namespace detail
} // namespace fire_engine
