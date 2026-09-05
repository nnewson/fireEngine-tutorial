#include "fire_engine/graphics/detail/frame_capture.hpp"

#include "fire_engine/gltf/detail/image_decoder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
class TemporaryDirectory final
{
public:
    TemporaryDirectory()
        : path_{std::filesystem::temp_directory_path() /
                ("fire-engine-capture-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(std::random_device{}()))}
    {
        if (!std::filesystem::create_directory(path_))
        {
            throw std::runtime_error("Could not create frame-capture test directory");
        }
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path_, error));
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::byte byte(std::uint8_t value)
{
    return static_cast<std::byte>(value);
}

[[nodiscard]] std::vector<char> readBytes(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
} // namespace

static_assert(fire_engine::detail::captureByteSize(3, 12) == 36);
static_assert(fire_engine::detail::captureByteSize(std::numeric_limits<std::size_t>::max(), 2) ==
              std::numeric_limits<std::size_t>::max());

TEST_CASE("Frame capture preserves RGBA order and forces opaque alpha")
{
    const std::array mapped{
        byte(1), byte(2), byte(3), byte(4), byte(5), byte(6), byte(7), byte(8),
    };

    REQUIRE(fire_engine::detail::toRgba8(mapped, 2, 1, 8,
                                         fire_engine::detail::CaptureFormat::Rgba8Srgb) ==
            std::vector<std::uint8_t>{1, 2, 3, 255, 5, 6, 7, 255});
}

TEST_CASE("Frame capture converts BGRA order and skips row padding")
{
    const std::array mapped{
        byte(3),  byte(2),  byte(1),  byte(4),  byte(7),  byte(6),  byte(5),  byte(8),
        byte(90), byte(91), byte(92), byte(93), byte(13), byte(12), byte(11), byte(14),
        byte(17), byte(16), byte(15), byte(18), byte(94), byte(95), byte(96), byte(97),
    };

    REQUIRE(
        fire_engine::detail::toRgba8(mapped, 2, 2, 12,
                                     fire_engine::detail::CaptureFormat::Bgra8Srgb) ==
        std::vector<std::uint8_t>{1, 2, 3, 255, 5, 6, 7, 255, 11, 12, 13, 255, 15, 16, 17, 255});
}

TEST_CASE("Frame capture rejects invalid source geometry and formats")
{
    const std::array mapped{
        byte(1), byte(2), byte(3), byte(4), byte(5), byte(6), byte(7), byte(8),
    };

    SECTION("row pitch is shorter than one packed row")
    {
        REQUIRE(fire_engine::detail::toRgba8(mapped, 2, 1, 7,
                                             fire_engine::detail::CaptureFormat::Rgba8Srgb)
                    .empty());
    }
    SECTION("mapping is shorter than the pitched rows")
    {
        REQUIRE(fire_engine::detail::toRgba8(mapped, 1, 2, 8,
                                             fire_engine::detail::CaptureFormat::Rgba8Srgb)
                    .empty());
    }
    SECTION("capture format is unsupported")
    {
        REQUIRE(fire_engine::detail::toRgba8(mapped, 2, 1, 8,
                                             static_cast<fire_engine::detail::CaptureFormat>(255))
                    .empty());
    }
    SECTION("source geometry overflows its byte count")
    {
        REQUIRE(fire_engine::detail::toRgba8(mapped, std::numeric_limits<std::size_t>::max(), 1, 8,
                                             fire_engine::detail::CaptureFormat::Rgba8Srgb)
                    .empty());
        REQUIRE(fire_engine::detail::toRgba8(mapped, 1, std::numeric_limits<std::size_t>::max(), 8,
                                             fire_engine::detail::CaptureFormat::Rgba8Srgb)
                    .empty());
    }
}

TEST_CASE("Frame capture writes a PNG and reports write failures")
{
    const TemporaryDirectory temporary;
    constexpr std::array<std::uint8_t, 8> rgba{1, 2, 3, 255, 4, 5, 6, 255};

    const std::filesystem::path output = temporary.path() / "capture.png";
    const std::filesystem::path repeatedOutput = temporary.path() / "capture-again.png";
    REQUIRE(fire_engine::detail::writeRgba8Png(output, rgba, 2, 1));
    REQUIRE(fire_engine::detail::writeRgba8Png(repeatedOutput, rgba, 2, 1));
    REQUIRE(std::filesystem::is_regular_file(output));
    REQUIRE(std::filesystem::is_regular_file(repeatedOutput));

    std::ifstream file{output, std::ios::binary};
    std::array<unsigned char, 8> signature{};
    file.read(reinterpret_cast<char*>(signature.data()),
              static_cast<std::streamsize>(signature.size()));
    constexpr std::array<unsigned char, 8> kPngSignature{137, 80, 78, 71, 13, 10, 26, 10};
    REQUIRE(signature == kPngSignature);

    // Reuse the glTF decoder only as a test oracle. Production graphics code
    // remains independent of glTF internals and does not gain this dependency.
    const fire_engine::ImageData decoded = fire_engine::detail::decodeRgba8(output);
    REQUIRE(decoded.width == 2);
    REQUIRE(decoded.height == 1);
    REQUIRE(decoded.pixels == std::vector<std::uint8_t>{rgba.begin(), rgba.end()});

    const std::vector<char> firstEncoding = readBytes(output);
    REQUIRE_FALSE(firstEncoding.empty());
    // This pins deterministic encoding within one process. The reference-image
    // contract separately requires matching captures from independent runs.
    REQUIRE(firstEncoding == readBytes(repeatedOutput));

    REQUIRE_FALSE(fire_engine::detail::writeRgba8Png(temporary.path() / "missing" / "capture.png",
                                                     rgba, 2, 1));
    REQUIRE_FALSE(fire_engine::detail::writeRgba8Png(
        temporary.path() / "short.png", std::span<const std::uint8_t>{rgba}.first(7), 2, 1));
}
