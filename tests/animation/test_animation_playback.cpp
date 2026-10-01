#include "fire_engine/animation/animation_playback.hpp"

#include "fire_engine/animation/animation.hpp"
#include "fire_engine/gltf/gltf_loader.hpp"
#include "fire_engine/graphics/render_assets.hpp"
#include "fire_engine/graphics/render_preparation.hpp"
#include "fire_engine/scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <numbers>
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

TEST_CASE("Animation playback uses constant angular steps between rotation keys")
{
    Animation animation = makeAnimation();
    SECTION("unit key values")
    {
    }
    SECTION("finite nonzero application keys need not be unit quaternions")
    {
        // The loader normalizes its keys, but animation validation accepts this wider
        // application contract. Unequal magnitudes must not change angular pacing.
        const std::array scales{4.0f, -0.25f, 1.0e20f};
        for (std::size_t index = 0; index < scales.size(); ++index)
        {
            auto& value = animation.channels.front().values[index];
            value = {.x = value.x * scales[index],
                     .y = value.y * scales[index],
                     .z = value.z * scales[index],
                     .w = value.w * scales[index]};
        }
    }
    const std::vector animations{animation};
    Scene scene;
    SceneNode& node = scene.addRoot("animator");
    node.component(makeAnimator());

    for (unsigned int step = 1; step <= 8; ++step)
    {
        CAPTURE(step);
        fire_engine::advanceAnimations(scene, animations, 0.25f);
        const float time = static_cast<float>(step % 8) * 0.25f;
        REQUIRE(animator(node).playbackTime == time);
        const double halfAngle = time * std::numbers::pi / 4.0;
        const Quaternion expected{.z = static_cast<float>(std::sin(halfAngle)),
                                  .w = static_cast<float>(std::cos(halfAngle))};
        auto actual = node.localTransform().rotation;
        if (actual.dot(expected) < 0.0f)
        {
            actual = -actual;
        }
        // Absolute component tolerance. Quarter intervals, not just midpoints, reject
        // replacing sampleRotation's SLERP call with the old NLERP call.
        REQUIRE(std::abs(actual.x - expected.x) <= 1.0e-6f);
        REQUIRE(std::abs(actual.y - expected.y) <= 1.0e-6f);
        REQUIRE(std::abs(actual.z - expected.z) <= 1.0e-6f);
        REQUIRE(std::abs(actual.w - expected.w) <= 1.0e-6f);
        REQUIRE(std::abs(actual.lengthSquared() - 1.0f) <= 1.0e-6f);
    }
}

TEST_CASE("AnimatedCube retains its imported shortest-arc direction across keys and the loop seam")
{
    auto content =
        fire_engine::GltfLoader{}.load(std::filesystem::path{FIRE_ENGINE_TEST_ASSET_DIRECTORY} /
                                       "AnimatedCube" / "AnimatedCube.gltf");
    REQUIRE(content.animations.size() == 1);
    REQUIRE(content.animations.front().channels.size() == 1);
    const auto& channel = content.animations.front().channels.front();
    REQUIRE(channel.timestamps == std::vector{0.0f, 1.0f, 2.0f});
    REQUIRE(channel.values.size() == 3);
    // These are imported near-half-turns, not exact ties. Their small negative dots
    // select negative-Y rotation through both intervals; do not idealize the keys.
    // This guards a sensitive tie boundary, not just generic shortest-path behavior:
    // flipping only one interval's rounding residual would reverse half the spin.
    for (std::size_t index = 1; index < channel.values.size(); ++index)
    {
        const float cosine = channel.values[index - 1].dot(channel.values[index]);
        REQUIRE(cosine < 0.0f);
        REQUIRE(cosine > -1.0e-6f);
    }
    REQUIRE(content.scene.roots().size() == 1);
    SceneNode& node = *content.scene.roots().front();
    REQUIRE(std::holds_alternative<Animator>(node.component()));
    const Animator initial = animator(node);
    for (const float elapsed :
         {0.0f, 0.001f, 0.25f, 0.75f, 0.999f, 1.0f, 1.001f, 1.25f, 1.75f, 1.999f, 2.0f, 2.001f})
    {
        CAPTURE(elapsed);
        node.component(initial);
        fire_engine::advanceAnimations(content.scene, content.animations, elapsed);
        const float time = std::fmod(elapsed, 2.0f);
        REQUIRE(animator(node).playbackTime == time);
        const double halfAngle = -time * std::numbers::pi / 2.0;
        const Quaternion expected{.y = static_cast<float>(std::sin(halfAngle)),
                                  .w = static_cast<float>(std::cos(halfAngle))};
        auto actual = node.localTransform().rotation;
        if (actual.dot(expected) < 0.0f)
        {
            actual = -actual;
        }
        // Permit imported float error around nominal 180-degree keys, not a different
        // arc or nonlinear pacing. Opposite signs may encode the same boundary rotation.
        REQUIRE(std::abs(actual.x - expected.x) <= 2.0e-6f);
        REQUIRE(std::abs(actual.y - expected.y) <= 2.0e-6f);
        REQUIRE(std::abs(actual.z - expected.z) <= 2.0e-6f);
        REQUIRE(std::abs(actual.w - expected.w) <= 2.0e-6f);
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
    fire_engine::SceneDrawListArena drawListArena;
    static_cast<void>(preparation.build(assets, scene.buildDrawItems(drawListArena),
                                        fire_engine::PipelineDescription{}));
    REQUIRE(preparation.generation() == 1);

    fire_engine::advanceAnimations(scene, animations, 0.5f);
    scene.updateWorldTransforms();
    static_cast<void>(preparation.build(assets, scene.buildDrawItems(drawListArena),
                                        fire_engine::PipelineDescription{}));

    REQUIRE(preparation.generation() == 1);
    REQUIRE(scene.buildDrawItems(drawListArena).drawItems.front().world !=
            fire_engine::Mat4::identity());
}
