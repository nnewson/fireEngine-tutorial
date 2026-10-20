#include "fire_engine/scene/scene_snapshot.hpp"

#include "fire_engine/graphics/render_assets.hpp"
#include "fire_engine/scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using fire_engine::DrawItem;
using fire_engine::Material;
using fire_engine::RenderAssets;
using fire_engine::RenderAssetsStamp;
using fire_engine::RenderObjectId;
using fire_engine::Scene;
using fire_engine::SceneNode;
using fire_engine::SceneSnapshot;
using fire_engine::Transform;

static_assert(!std::is_copy_constructible_v<SceneSnapshot>);
static_assert(!std::is_copy_assignable_v<SceneSnapshot>);
static_assert(std::is_nothrow_default_constructible_v<SceneSnapshot>);
static_assert(std::is_nothrow_move_constructible_v<SceneSnapshot>);
static_assert(std::is_nothrow_move_assignable_v<SceneSnapshot>);

/** @brief Builds a two-draw snapshot whose draws carry distinct objects. */
SceneSnapshot twoDraws(const RenderAssets& catalog)
{
    return SceneSnapshot{catalog,
                         {DrawItem{.renderObject = RenderObjectId{.value = 0}},
                          DrawItem{.renderObject = RenderObjectId{.value = 1}}}};
}

/** @brief Requires a snapshot to hold no draws and no catalog. */
void requireEnded(const SceneSnapshot& snapshot)
{
    REQUIRE(snapshot.drawItems().empty());
    REQUIRE_FALSE(snapshot.catalog().has_value());
}
} // namespace

TEST_CASE("A default scene snapshot is empty with no catalog")
{
    requireEnded(SceneSnapshot{});
}

TEST_CASE("A scene snapshot keeps its draws in order with its catalog's stamp")
{
    const RenderAssets catalog;
    const SceneSnapshot snapshot = twoDraws(catalog);

    REQUIRE(snapshot.catalog() == catalog.stamp());
    REQUIRE(snapshot.drawItems().size() == 2);
    REQUIRE(snapshot.drawItems()[0].renderObject == RenderObjectId{.value = 0});
    REQUIRE(snapshot.drawItems()[1].renderObject == RenderObjectId{.value = 1});
}

TEST_CASE("A scene snapshot keeps the catalog stamp from when it was built")
{
    RenderAssets catalog;
    const SceneSnapshot snapshot = twoDraws(catalog);
    const RenderAssetsStamp built = catalog.stamp();

    static_cast<void>(catalog.addMaterial(Material{}));

    REQUIRE(snapshot.catalog() == built);
    REQUIRE(catalog.stamp() != built);
}

TEST_CASE("A scene snapshot is unchanged by later builds")
{
    // A draw-list view backed by a shared arena changed here: the second
    // build overwrote the storage the first view still referred to.
    const RenderAssets catalog;
    Scene scene;
    SceneNode& node = scene.addRoot("node");
    node.component(RenderObjectId{.value = 0});
    scene.updateWorldTransforms();
    const SceneSnapshot first = scene.buildSnapshot(catalog);
    const auto originalWorld = first.drawItems().front().world;

    node.localTransform(Transform{.translation = {.x = 5.0f, .y = 0.0f, .z = 0.0f}});
    scene.updateWorldTransforms();
    const SceneSnapshot second = scene.buildSnapshot(catalog);

    REQUIRE(second.drawItems().front().world != originalWorld);
    REQUIRE(first.drawItems().front().world == originalWorld);
}

TEST_CASE("Building a scene snapshot discards what its recycled storage held")
{
    const RenderAssets catalog;
    Scene scene;
    scene.addRoot("node").component(RenderObjectId{.value = 3});
    std::vector<DrawItem> storage{DrawItem{.renderObject = RenderObjectId{.value = 7}},
                                  DrawItem{.renderObject = RenderObjectId{.value = 8}}};

    const SceneSnapshot snapshot = scene.buildSnapshot(catalog, std::move(storage));

    REQUIRE(snapshot.drawItems().size() == 1);
    REQUIRE(snapshot.drawItems().front().renderObject == RenderObjectId{.value = 3});
}

TEST_CASE("Releasing a scene snapshot's storage ends the snapshot")
{
    const RenderAssets catalog;
    SceneSnapshot snapshot = twoDraws(catalog);
    const DrawItem* const buffer = snapshot.drawItems().data();

    const std::vector<DrawItem> storage = std::move(snapshot).releaseStorage();

    REQUIRE(storage.data() == buffer);
    REQUIRE(storage.size() == 2);
    // NOLINTNEXTLINE(bugprone-use-after-move): the released state is part of the contract.
    requireEnded(snapshot);
}

TEST_CASE("Moving a scene snapshot leaves the source empty with no catalog")
{
    const RenderAssets catalog;
    SceneSnapshot source = twoDraws(catalog);

    SceneSnapshot constructed{std::move(source)};
    REQUIRE(constructed.catalog() == catalog.stamp());
    REQUIRE(constructed.drawItems().size() == 2);
    // NOLINTNEXTLINE(bugprone-use-after-move): the moved-from state is part of the contract.
    requireEnded(source);

    SceneSnapshot assigned;
    assigned = std::move(constructed);
    REQUIRE(assigned.catalog() == catalog.stamp());
    REQUIRE(assigned.drawItems().size() == 2);
    // NOLINTNEXTLINE(bugprone-use-after-move): the moved-from state is part of the contract.
    requireEnded(constructed);

    SceneSnapshot& alias = assigned;
    assigned = std::move(alias);
    REQUIRE(assigned.catalog() == catalog.stamp());
    REQUIRE(assigned.drawItems().size() == 2);
}
