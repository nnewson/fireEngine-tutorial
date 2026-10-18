#include <fire_engine/render/detail/compiled_resources.hpp>

#include <fire_engine/render/detail/compiled_resource_graph.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

// Empty graphs own no Vulkan objects, so the last-use contract is testable
// without a device. Whether the renderer marks use at its commit point, and
// waits for the tag before replacing, is wiring covered by device runs.

namespace
{
using fire_engine::detail::CompiledResourceGraph;
using fire_engine::detail::CompiledResources;

// Above 32 bits, so a narrowed tag cannot round-trip.
constexpr std::uint64_t kFirstUse = 0x1'0000'0003;

static_assert(noexcept(std::declval<CompiledResources&>().markUsed(kFirstUse)));
static_assert(noexcept(std::declval<const CompiledResources&>().lastUse()));
static_assert(noexcept(std::declval<CompiledResources&>().replace(
    std::declval<std::unique_ptr<CompiledResourceGraph>>())));
} // namespace

TEST_CASE("Compiled resources start with no last use")
{
    const CompiledResources resources;
    REQUIRE_FALSE(resources.lastUse().has_value());
}

TEST_CASE("Compiled resources record the latest submission that used the graph")
{
    CompiledResources resources;
    resources.markUsed(kFirstUse);
    REQUIRE(resources.lastUse() == std::optional<std::uint64_t>{kFirstUse});

    resources.markUsed(kFirstUse + 1);
    REQUIRE(resources.lastUse() == std::optional<std::uint64_t>{kFirstUse + 1});
}

TEST_CASE("Replacing compiled resources installs a graph no submission has used")
{
    CompiledResources resources;
    resources.markUsed(kFirstUse);
    resources.markUsed(kFirstUse + 1);

    auto replacement = std::make_unique<CompiledResourceGraph>();
    const CompiledResourceGraph* const candidate = replacement.get();
    resources.replace(std::move(replacement));
    REQUIRE(&resources.graph() == candidate);
    REQUIRE_FALSE(resources.lastUse().has_value());

    // The replacement's first submission starts its own tag.
    resources.markUsed(kFirstUse + 2);
    REQUIRE(resources.lastUse() == std::optional<std::uint64_t>{kFirstUse + 2});
}
