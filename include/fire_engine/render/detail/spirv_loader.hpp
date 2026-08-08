#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/**
 * @brief Loads one SPIR-V binary as complete 32-bit words.
 * @param path Filesystem path to the binary module.
 * @return File contents in their original word order.
 * @throws std::runtime_error if the file is missing, empty, malformed, or unreadable.
 */
[[nodiscard]] std::vector<std::uint32_t> loadSpirv(std::string_view path);
/** @endcond */
} // namespace fire_engine::detail
