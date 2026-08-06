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
using fire_engine::Vec3;
} // namespace

TEST_CASE("Scene resolves transforms and emits draw items depth first")
{
    Scene scene;

    auto root = std::make_unique<SceneNode>("root");
    root->localTransform(Mat4::translation(Vec3{.x = 1.0f}));
    root->renderObject(RenderObjectId{.value = 0});

    auto child = std::make_unique<SceneNode>("child");
    child->localTransform(Mat4::translation(Vec3{.y = 2.0f}));
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
