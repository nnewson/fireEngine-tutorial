#include "fire_engine/animation/animation_playback.hpp"

#include "fire_engine/animation/animation.hpp"
#include "fire_engine/graphics/render_assets.hpp"
#include "fire_engine/graphics/render_preparation.hpp"
#include "fire_engine/scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <variant>
#include <vector>

namespace
{
using fire_engine::Animation;
using fire_engine::AnimationChannel;
using fire_engine::AnimationChannelId;
using fire_engine::AnimationId;
using fire_engine::AnimationTargetPath;
using fire_engine::Animator;
using fire_engine::Material;
using fire_engine::Mesh;
using fire_engine::Quaternion;
using fire_engine::RenderAssets;
using fire_engine::RenderObject;
using fire_engine::RenderPreparation;
using fire_engine::Scene;
using fire_engine::SceneNode;
using fire_engine::Vec3;
using fire_engine::Vertex;

constexpr float kQuarterTurnComponent = 0.70710678f;

Animation makeAnimation()
{
    return {
        .name = "turn",
        .channels =
            {
                AnimationChannel{
                    .timestamps = {0.0f, 1.0f, 2.0f},
                    .values =
                        {
                            Quaternion::identity(),
                            Quaternion{.z = kQuarterTurnComponent, .w = kQuarterTurnComponent},
                            Quaternion{.z = 1.0f, .w = 0.0f},
                        },
                },
            },
    };
}

Animator makeAnimator(bool looping = true)
{
    return {
        .animation = AnimationId{.value = 0},
        .channel = AnimationChannelId{.value = 0},
        .targetPath = AnimationTargetPath::eRotation,
        .playbackTime = 0.0f,
        .looping = looping,
    };
}

const Animator& animator(const SceneNode& node)
{
    return std::get<Animator>(node.component());
}

Mesh makeTriangle()
{
    return {
        .vertices =
            {
                Vertex{.position = Vec3{.x = -0.5f}, .color = {}, .textureCoordinate = {}},
                Vertex{.position = Vec3{.x = 0.5f}, .color = {}, .textureCoordinate = {}},
                Vertex{.position = Vec3{.y = 0.5f}, .color = {}, .textureCoordinate = {}},
            },
        .indices = {0, 1, 2},
    };
}
} // namespace

TEST_CASE("Animation playback interpolates normalized rotations at stable keyframe boundaries")
{
    const std::vector animations{makeAnimation()};
    Scene scene;
    SceneNode& node = scene.addRoot("animator");
    node.component(makeAnimator());

    fire_engine::advanceAnimations(scene, animations, 0.5f);
    REQUIRE(animator(node).playbackTime == 0.5f);
    REQUIRE(node.localTransform().rotation.lengthSquared() == Catch::Approx(1.0f));

    fire_engine::advanceAnimations(scene, animations, 0.5f);
    REQUIRE(animator(node).playbackTime == 1.0f);
    REQUIRE(node.localTransform().rotation.z == Catch::Approx(kQuarterTurnComponent));
    REQUIRE(node.localTransform().rotation.w == Catch::Approx(kQuarterTurnComponent));
}

TEST_CASE("Animation playback wraps looping channels and clamps non-looping channels")
{
    const std::vector animations{makeAnimation()};
    Scene scene;
    SceneNode& node = scene.addRoot("animator");

    SECTION("looping playback wraps across the duration")
    {
        node.component(makeAnimator());
        fire_engine::advanceAnimations(scene, animations, 2.5f);

        REQUIRE(animator(node).playbackTime == 0.5f);
        REQUIRE(node.localTransform().rotation.lengthSquared() == Catch::Approx(1.0f));
    }
    SECTION("the exact loop duration returns to the first keyframe")
    {
        node.component(makeAnimator());
        fire_engine::advanceAnimations(scene, animations, 2.0f);

        REQUIRE(animator(node).playbackTime == 0.0f);
        REQUIRE(node.localTransform().rotation == Quaternion::identity());
    }
    SECTION("non-looping playback holds the final keyframe")
    {
        node.component(makeAnimator(false));
        fire_engine::advanceAnimations(scene, animations, 3.0f);

        REQUIRE(animator(node).playbackTime == 2.0f);
        REQUIRE(node.localTransform().rotation == Quaternion{.z = 1.0f, .w = 0.0f});
    }
}

TEST_CASE("Animation changes transforms without invalidating render preparation")
{
    RenderAssets assets;
    const auto mesh = assets.addMesh(makeTriangle());
    const auto material = assets.addMaterial(Material{});
    const auto object = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});

    const std::vector animations{makeAnimation()};
    Scene scene;
    SceneNode& animated = scene.addRoot("animator");
    animated.component(makeAnimator());
    scene.addChild(animated, std::make_unique<SceneNode>("renderable")).component(object);
    scene.updateWorldTransforms();

    RenderPreparation preparation;
    static_cast<void>(preparation.build(assets, scene.buildDrawItems()));
    REQUIRE(preparation.generation() == 1);

    fire_engine::advanceAnimations(scene, animations, 0.5f);
    scene.updateWorldTransforms();
    static_cast<void>(preparation.build(assets, scene.buildDrawItems()));

    REQUIRE(preparation.generation() == 1);
    REQUIRE(scene.buildDrawItems().drawItems.front().world != fire_engine::Mat4::identity());
}
