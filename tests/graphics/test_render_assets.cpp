#include "fire_engine/graphics/render_assets.hpp"

#include "fire_engine/graphics/detail/render_assets_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using fire_engine::ImageData;
using fire_engine::ImageId;
using fire_engine::Material;
using fire_engine::MaterialId;
using fire_engine::Mesh;
using fire_engine::MeshId;
using fire_engine::RenderAssets;
using fire_engine::RenderAssetsId;
using fire_engine::RenderAssetsStamp;
using fire_engine::RenderObject;
using fire_engine::RenderObjectId;
using fire_engine::Texture;
using fire_engine::TextureId;

// These pin the catalog's const access and by-value ownership shape. They
// cannot rule out a caller writing through an alias it kept to storage it
// transferred in; the class documentation makes that the caller's obligation.
static_assert(
    std::is_same_v<decltype(std::declval<RenderAssets&>().meshes()), const std::vector<Mesh>&>);
static_assert(std::is_same_v<decltype(std::declval<RenderAssets&>().images()),
                             const std::vector<ImageData>&>);
static_assert(std::is_same_v<decltype(std::declval<RenderAssets&>().textures()),
                             const std::vector<Texture>&>);
static_assert(std::is_same_v<decltype(std::declval<RenderAssets&>().materials()),
                             const std::vector<Material>&>);
static_assert(std::is_same_v<decltype(std::declval<RenderAssets&>().renderObjects()),
                             const std::vector<RenderObject>&>);
static_assert(std::is_same_v<decltype(&RenderAssets::addMesh), MeshId (RenderAssets::*)(Mesh)>);
static_assert(
    std::is_same_v<decltype(&RenderAssets::addImage), ImageId (RenderAssets::*)(ImageData)>);
static_assert(
    std::is_same_v<decltype(&RenderAssets::addTexture), TextureId (RenderAssets::*)(Texture)>);
static_assert(
    std::is_same_v<decltype(&RenderAssets::addMaterial), MaterialId (RenderAssets::*)(Material)>);
static_assert(std::is_same_v<decltype(&RenderAssets::addRenderObject),
                             RenderObjectId (RenderAssets::*)(RenderObject)>);
static_assert(!std::is_copy_constructible_v<RenderAssets>);
static_assert(!std::is_copy_assignable_v<RenderAssets>);
static_assert(std::is_nothrow_default_constructible_v<RenderAssets>);
static_assert(std::is_nothrow_move_constructible_v<RenderAssets>);
static_assert(std::is_nothrow_move_assignable_v<RenderAssets>);

/** @brief Adds one entry of every kind, advancing the revision by five. */
void addOneOfEach(RenderAssets& assets)
{
    const MeshId mesh = assets.addMesh(Mesh{});
    const ImageId image = assets.addImage(ImageData{});
    static_cast<void>(assets.addTexture(Texture{.image = image}));
    const MaterialId material = assets.addMaterial(Material{});
    static_cast<void>(assets.addRenderObject(RenderObject{.mesh = mesh, .material = material}));
}

/** @brief Requires a catalog to hold no entries of any kind. */
void requireEmpty(const RenderAssets& assets)
{
    REQUIRE(assets.meshes().empty());
    REQUIRE(assets.images().empty());
    REQUIRE(assets.textures().empty());
    REQUIRE(assets.materials().empty());
    REQUIRE(assets.renderObjects().empty());
}

/** @brief Requires a catalog to hold exactly the entries added by addOneOfEach(). */
void requireOneOfEach(const RenderAssets& assets)
{
    REQUIRE(assets.meshes().size() == 1);
    REQUIRE(assets.images().size() == 1);
    REQUIRE(assets.textures().size() == 1);
    REQUIRE(assets.materials().size() == 1);
    REQUIRE(assets.renderObjects().size() == 1);
}
} // namespace

TEST_CASE("Render asset catalogs start with distinct identities at revision zero")
{
    const RenderAssets first;
    const RenderAssets second;

    REQUIRE(first.id().valid());
    REQUIRE(second.id().valid());
    REQUIRE(first.id() != second.id());
    REQUIRE(first.revision() == 0);
    REQUIRE(first.stamp() == RenderAssetsStamp{.catalog = first.id(), .revision = 0});
    requireEmpty(first);
}

TEST_CASE("A catalog built where another was destroyed has a new identity")
{
    std::optional<RenderAssets> assets{std::in_place};
    const RenderAssets* const address = &*assets;
    const RenderAssetsId destroyed = assets->id();

    assets.reset();
    assets.emplace();

    REQUIRE(&*assets == address);
    REQUIRE(assets->revision() == 0);
    REQUIRE(assets->id() != destroyed);
}

TEST_CASE("Adding descriptions keeps a catalog's identity and advances its revision")
{
    RenderAssets assets;
    const RenderAssetsId id = assets.id();

    addOneOfEach(assets);

    REQUIRE(assets.id() == id);
    REQUIRE(assets.revision() == 5);
    REQUIRE(assets.stamp() == RenderAssetsStamp{.catalog = id, .revision = 5});
}

TEST_CASE("Moving a catalog carries its identity, revision and content")
{
    RenderAssets source;
    addOneOfEach(source);
    const RenderAssetsStamp original = source.stamp();

    const RenderAssets destination{std::move(source)};

    REQUIRE(destination.stamp() == original);
    requireOneOfEach(destination);
    // NOLINTBEGIN(bugprone-use-after-move): the moved-from state is part of the contract.
    REQUIRE(source.id().valid());
    REQUIRE(source.id() != original.catalog);
    REQUIRE(source.revision() == 0);
    requireEmpty(source);

    // The restarted source is an ordinary catalog with its own history.
    const RenderAssetsId restarted = source.id();
    addOneOfEach(source);
    REQUIRE(source.stamp() == RenderAssetsStamp{.catalog = restarted, .revision = 5});
    // NOLINTEND(bugprone-use-after-move)
    REQUIRE(destination.stamp() == original);
}

TEST_CASE("Move-assigning a catalog replaces the destination's identity with the source's")
{
    RenderAssets source;
    addOneOfEach(source);
    const RenderAssetsStamp original = source.stamp();
    RenderAssets destination;
    static_cast<void>(destination.addMaterial(Material{}));
    const RenderAssetsId replaced = destination.id();

    destination = std::move(source);

    REQUIRE(destination.stamp() == original);
    requireOneOfEach(destination);
    // NOLINTBEGIN(bugprone-use-after-move): the moved-from state is part of the contract.
    REQUIRE(source.id().valid());
    REQUIRE(source.id() != original.catalog);
    REQUIRE(source.id() != replaced);
    REQUIRE(source.revision() == 0);
    requireEmpty(source);
    // NOLINTEND(bugprone-use-after-move)
}

TEST_CASE("Self-move-assignment leaves a catalog unchanged")
{
    RenderAssets assets;
    addOneOfEach(assets);
    const RenderAssetsStamp original = assets.stamp();
    RenderAssets& alias = assets;

    assets = std::move(alias);

    REQUIRE(assets.stamp() == original);
    requireOneOfEach(assets);
}

TEST_CASE("Catalog identity issuance fails closed when exhausted")
{
    constexpr std::uint64_t kLargest = std::numeric_limits<std::uint64_t>::max();
    std::atomic<std::uint64_t> counter{kLargest - 1};

    REQUIRE(fire_engine::detail::issueRenderAssetsId(counter) == kLargest - 1);
    REQUIRE(fire_engine::detail::issueRenderAssetsId(counter) == kLargest);
    REQUIRE(counter.load() == 0);
    REQUIRE_FALSE(fire_engine::detail::issueRenderAssetsId(counter).has_value());
    REQUIRE_FALSE(fire_engine::detail::issueRenderAssetsId(counter).has_value());
    REQUIRE(counter.load() == 0);
}
