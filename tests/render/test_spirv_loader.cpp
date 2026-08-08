#include "fire_engine/render/detail/spirv_loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
class TemporaryPath final
{
public:
    TemporaryPath()
        : path_{std::filesystem::temp_directory_path() /
                ("fire-engine-spirv-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(std::random_device{}()))}
    {
    }

    ~TemporaryPath()
    {
        std::error_code error;
        static_cast<void>(std::filesystem::remove(path_, error));
    }

    TemporaryPath(const TemporaryPath&) = delete;
    TemporaryPath& operator=(const TemporaryPath&) = delete;
    TemporaryPath(TemporaryPath&&) = delete;
    TemporaryPath& operator=(TemporaryPath&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};
} // namespace

TEST_CASE("SPIR-V loading preserves complete words")
{
    const TemporaryPath temporary;
    constexpr std::array words = {
        std::uint32_t{0x07230203},
        std::uint32_t{0x00010600},
        std::uint32_t{0x12345678},
    };
    std::ofstream file{temporary.path(), std::ios::binary};
    file.write(reinterpret_cast<const char*>(words.data()),
               static_cast<std::streamsize>(sizeof(words)));
    file.close();
    REQUIRE(file.good());

    const std::vector loaded = fire_engine::detail::loadSpirv(temporary.path().string());
    REQUIRE(loaded == std::vector<std::uint32_t>{words.begin(), words.end()});
}

TEST_CASE("SPIR-V loading rejects unusable files")
{
    const TemporaryPath temporary;

    SECTION("missing file")
    {
        REQUIRE_THROWS_AS(fire_engine::detail::loadSpirv(temporary.path().string()),
                          std::runtime_error);
    }
    SECTION("empty file")
    {
        std::ofstream file{temporary.path(), std::ios::binary};
        file.close();
        REQUIRE_THROWS_AS(fire_engine::detail::loadSpirv(temporary.path().string()),
                          std::runtime_error);
    }
    SECTION("partial word")
    {
        std::ofstream file{temporary.path(), std::ios::binary};
        constexpr std::array bytes = {'S', 'P', 'V'};
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        REQUIRE_THROWS_AS(fire_engine::detail::loadSpirv(temporary.path().string()),
                          std::runtime_error);
    }
}
