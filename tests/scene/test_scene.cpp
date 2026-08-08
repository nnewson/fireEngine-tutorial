#include "fire_engine/scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
using fire_engine::DrawItem;
using fire_engine::Mat4;
using fire_engine::RenderObjectId;
using fire_engine::Scene;
using fire_engine::SceneNode;
using fire_engine::SceneNodeId;
using fire_engine::Transform;
using fire_engine::Vec3;
} // namespace

TEST_CASE("Scene resolves transforms and emits draw items depth first")
{
    Scene scene;

    auto root = std::make_unique<SceneNode>("root");
    root->localTransform(Transform{
        .translation = {.x = 1.0f, .y = 0.0f, .z = 0.0f},
        .rotation = {},
        .scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f},
    });
    root->renderObject(RenderObjectId{.value = 0});

    auto child = std::make_unique<SceneNode>("child");
    child->localTransform(Transform{
        .translation = {.x = 0.0f, .y = 2.0f, .z = 0.0f},
        .rotation = {},
        .scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f},
    });
    child->renderObject(RenderObjectId{.value = 1});
    SceneNode& childReference = root->addChild(std::move(child));

    scene.addRoot(std::move(root));
    scene.updateWorldTransforms();

    REQUIRE(childReference.worldTransform()[0, 3] == 1.0f);
    REQUIRE(childReference.worldTransform()[1, 3] == 2.0f);

    const auto drawList = scene.buildDrawItems();
    REQUIRE(drawList.drawItems.size() == 2);
    REQUIRE(drawList.drawItems[0].renderObject == RenderObjectId{.value = 0});
    REQUIRE(drawList.drawItems[1].renderObject == RenderObjectId{.value = 1});
    REQUIRE(drawList.drawItems[1].world == childReference.worldTransform());
}

TEST_CASE("Scene assigns stable IDs and registers descendants added later")
{
    Scene scene;
    SceneNode& root = scene.addRoot("root");
    const auto rootId = root.id();

    REQUIRE(rootId.has_value());
    REQUIRE(rootId->valid());
    const auto foundRoot = scene.findNode(*rootId);
    REQUIRE(foundRoot.has_value());
    REQUIRE(&foundRoot->get() == &root);

    SceneNode& child = root.addChild("child");
    REQUIRE_FALSE(child.id().has_value());

    scene.updateWorldTransforms();
    const auto childId = child.id();
    REQUIRE(childId.has_value());
    REQUIRE(childId->valid());
    REQUIRE(childId != rootId);
    const auto foundChild = scene.findNode(*childId);
    REQUIRE(foundChild.has_value());
    REQUIRE(&foundChild->get() == &child);

    scene.updateWorldTransforms();
    REQUIRE(root.id() == rootId);
    REQUIRE(child.id() == childId);

    REQUIRE_FALSE(scene.findNode(SceneNodeId{}).has_value());
    REQUIRE_FALSE(scene.findNode(SceneNodeId{.value = 100}).has_value());

    const Scene& constScene = scene;
    const auto childNameLength = constScene.findNode(*childId).transform(
        [](fire_engine::SceneNodeConstRef node) { return node.get().name().size(); });
    REQUIRE(childNameLength == std::optional<std::size_t>{5});
}

TEST_CASE("Scene supports several roots in stable insertion order")
{
    Scene scene;
    SceneNode& first = scene.addRoot("first");
    first.renderObject(RenderObjectId{.value = 0});
    SceneNode& second = scene.addRoot("second");
    second.renderObject(RenderObjectId{.value = 1});
    scene.updateWorldTransforms();

    const auto drawList = scene.buildDrawItems();
    REQUIRE(drawList.drawItems.size() == 2);
    REQUIRE(drawList.drawItems[0].renderObject == RenderObjectId{.value = 0});
    REQUIRE(drawList.drawItems[1].renderObject == RenderObjectId{.value = 1});
}

TEST_CASE("Scene rejects null nodes")
{
    Scene scene;
    SceneNode node{"root"};

    REQUIRE_THROWS_AS(scene.addRoot(std::unique_ptr<SceneNode>{}), std::invalid_argument);
    REQUIRE_THROWS_AS(node.addChild(std::unique_ptr<SceneNode>{}), std::invalid_argument);
}
