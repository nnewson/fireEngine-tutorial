#include <fire_engine/render/detail/spirv_loader.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal functions --- */

std::vector<std::uint32_t> loadSpirv(std::string_view path)
{
    std::ifstream file{std::string{path}, std::ios::ate | std::ios::binary};
    if (!file)
    {
        throw std::runtime_error("Could not open compiled shader: " + std::string{path});
    }

    const std::streamoff byteCount = file.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0)
    {
        throw std::runtime_error("Compiled shader is not valid SPIR-V: " + std::string{path});
    }

    std::vector<std::uint32_t> code(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(byteCount));
    if (!file)
    {
        throw std::runtime_error("Could not read compiled shader: " + std::string{path});
    }
    return code;
}
/** @endcond */
} // namespace fire_engine::detail
