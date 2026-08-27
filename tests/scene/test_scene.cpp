#include "fire_engine/scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

namespace
{
using fire_engine::AnimationChannelId;
using fire_engine::AnimationId;
using fire_engine::AnimationTargetPath;
using fire_engine::Animator;
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
    root->component(RenderObjectId{.value = 0});

    auto child = std::make_unique<SceneNode>("child");
    child->localTransform(Transform{
        .translation = {.x = 0.0f, .y = 2.0f, .z = 0.0f},
        .rotation = {},
        .scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f},
    });
    child->component(RenderObjectId{.value = 1});
    SceneNode& childReference = root->addChild(std::move(child));

    scene.addRoot(std::move(root));
    REQUIRE(childReference.id().has_value());
    const auto registeredChild = scene.findNode(*childReference.id());
    REQUIRE(registeredChild.has_value());
    REQUIRE(&registeredChild->get() == &childReference);
    scene.updateWorldTransforms();

    REQUIRE(childReference.worldTransform()[0, 3] == 1.0f);
    REQUIRE(childReference.worldTransform()[1, 3] == 2.0f);

    const auto drawList = scene.buildDrawItems();
    REQUIRE(drawList.drawItems.size() == 2);
    REQUIRE(drawList.drawItems[0].renderObject == RenderObjectId{.value = 0});
    REQUIRE(drawList.drawItems[1].renderObject == RenderObjectId{.value = 1});
    REQUIRE(drawList.drawItems[1].world == childReference.worldTransform());
}

TEST_CASE("Scene registers attached subtrees immediately and preserves stable IDs")
{
    Scene scene;
    SceneNode& root = scene.addRoot("root");
    const auto rootId = root.id();

    REQUIRE(rootId.has_value());
    REQUIRE(rootId->valid());
    const auto foundRoot = scene.findNode(*rootId);
    REQUIRE(foundRoot.has_value());
    REQUIRE(&foundRoot->get() == &root);

    auto child = std::make_unique<SceneNode>("child");
    SceneNode& grandchild = child->addChild("grandchild");
    SceneNode& childReference = scene.addChild(root, std::move(child));

    const auto childId = childReference.id();
    REQUIRE(childId.has_value());
    REQUIRE(childId->valid());
    REQUIRE(childId != rootId);
    const auto foundChild = scene.findNode(*childId);
    REQUIRE(foundChild.has_value());
    REQUIRE(&foundChild->get() == &childReference);

    const auto grandchildId = grandchild.id();
    REQUIRE(grandchildId.has_value());
    REQUIRE(grandchildId->valid());
    const auto foundGrandchild = scene.findNode(*grandchildId);
    REQUIRE(foundGrandchild.has_value());
    REQUIRE(&foundGrandchild->get() == &grandchild);

    SceneNode& sibling = scene.addChild(*rootId, std::make_unique<SceneNode>("sibling"));
    const auto siblingId = sibling.id();
    REQUIRE(siblingId.has_value());
    REQUIRE(scene.findNode(*siblingId).has_value());

    scene.updateWorldTransforms();
    REQUIRE(root.id() == rootId);
    REQUIRE(childReference.id() == childId);
    REQUIRE(grandchild.id() == grandchildId);
    REQUIRE(sibling.id() == siblingId);

    scene.updateWorldTransforms();
    REQUIRE(childReference.id() == childId);
    REQUIRE(grandchild.id() == grandchildId);
    REQUIRE(sibling.id() == siblingId);

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
    first.component(RenderObjectId{.value = 0});
    SceneNode& second = scene.addRoot("second");
    second.component(RenderObjectId{.value = 1});
    scene.updateWorldTransforms();

    const auto drawList = scene.buildDrawItems();
    REQUIRE(drawList.drawItems.size() == 2);
    REQUIRE(drawList.drawItems[0].renderObject == RenderObjectId{.value = 0});
    REQUIRE(drawList.drawItems[1].renderObject == RenderObjectId{.value = 1});
}

TEST_CASE("Scene components separate animation behavior from renderable children")
{
    const Animator sharedAnimation{
        .animation = AnimationId{.value = 0},
        .channel = AnimationChannelId{.value = 0},
        .targetPath = AnimationTargetPath::eRotation,
        .playbackTime = 0.0f,
        .looping = true,
    };

    Scene scene;
    SceneNode& firstAnimator = scene.addRoot("first animator");
    firstAnimator.component(sharedAnimation);
    firstAnimator.localTransform(Transform{
        .translation = {.x = 3.0f, .y = 0.0f, .z = 0.0f},
        .rotation = {},
        .scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f},
    });
    scene.addChild(firstAnimator, std::make_unique<SceneNode>("first renderable"))
        .component(RenderObjectId{.value = 0});

    SceneNode& secondAnimator = scene.addRoot("second animator");
    secondAnimator.component(sharedAnimation);
    scene.addChild(secondAnimator, std::make_unique<SceneNode>("second renderable"))
        .component(RenderObjectId{.value = 1});

    SceneNode& transformOnly = scene.addRoot("transform only");

    REQUIRE(std::get<Animator>(firstAnimator.component()).animation == sharedAnimation.animation);
    REQUIRE(std::get<Animator>(secondAnimator.component()).channel == sharedAnimation.channel);
    REQUIRE(std::holds_alternative<std::monostate>(transformOnly.component()));

    scene.updateWorldTransforms();
    const auto drawList = scene.buildDrawItems();
    REQUIRE(drawList.drawItems.size() == 2);
    REQUIRE(drawList.drawItems[0].renderObject == RenderObjectId{.value = 0});
    REQUIRE(drawList.drawItems[0].world[0, 3] == 3.0f);
    REQUIRE(drawList.drawItems[1].renderObject == RenderObjectId{.value = 1});
}

TEST_CASE("Scene rejects null nodes")
{
    Scene scene;
    SceneNode node{"root"};
    SceneNode& root = scene.addRoot("registered root");
    Scene otherScene;
    SceneNode& foreignRoot = otherScene.addRoot("foreign root");

    REQUIRE_THROWS_AS(scene.addRoot(std::unique_ptr<SceneNode>{}), std::invalid_argument);
    REQUIRE_THROWS_AS(node.addChild(std::unique_ptr<SceneNode>{}), std::invalid_argument);
    REQUIRE_THROWS_WITH(scene.addChild(*root.id(), std::unique_ptr<SceneNode>{}),
                        Catch::Matchers::ContainsSubstring("cannot be null"));
    REQUIRE_THROWS_WITH(scene.addChild(foreignRoot, std::unique_ptr<SceneNode>{}),
                        Catch::Matchers::ContainsSubstring("cannot be null"));
}

TEST_CASE("Registered nodes require Scene child insertion")
{
    Scene scene;
    SceneNode& root = scene.addRoot("root");

    REQUIRE_THROWS_WITH(root.addChild("child"),
                        Catch::Matchers::ContainsSubstring("Scene::addChild"));
    REQUIRE(root.children().empty());
}

TEST_CASE("Scene rejects child insertion through foreign parents")
{
    Scene firstScene;
    SceneNode& firstRoot = firstScene.addRoot("first root");
    Scene secondScene;
    SceneNode& secondRoot = secondScene.addRoot("second root");

    REQUIRE(firstRoot.id() == secondRoot.id());
    REQUIRE_THROWS_AS(firstScene.addChild(secondRoot, std::make_unique<SceneNode>("foreign child")),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(firstScene.addChild(SceneNodeId{.value = 100},
                                          std::make_unique<SceneNode>("invalid parent child")),
                      std::invalid_argument);
    REQUIRE(firstRoot.children().empty());
    REQUIRE(secondRoot.children().empty());
}
