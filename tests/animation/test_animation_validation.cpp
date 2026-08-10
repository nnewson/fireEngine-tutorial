#include "fire_engine/animation/animation.hpp"
#include "fire_engine/animation/detail/animation_validation.hpp"
#include "fire_engine/scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
using fire_engine::Animation;
using fire_engine::AnimationChannel;
using fire_engine::AnimationChannelId;
using fire_engine::AnimationId;
using fire_engine::AnimationTargetPath;
using fire_engine::Animator;
using fire_engine::Quaternion;
using fire_engine::Scene;

Animation makeAnimation()
{
    return {
        .name = "turn",
        .channels =
            {
                AnimationChannel{
                    .timestamps = {0.0f, 1.0f},
                    .values = {Quaternion::identity(), Quaternion::identity()},
                },
            },
    };
}

Animator makeAnimator()
{
    return {
        .animation = AnimationId{.value = 0},
        .channel = AnimationChannelId{.value = 0},
        .targetPath = AnimationTargetPath::eRotation,
        .playbackTime = 0.0f,
        .looping = true,
    };
}
} // namespace

TEST_CASE("Animation validation accepts a complete target-independent channel")
{
    REQUIRE_NOTHROW(fire_engine::detail::validateAnimation(makeAnimation()));
}

TEST_CASE("Animation validation rejects malformed timelines")
{
    Animation animation = makeAnimation();
    AnimationChannel& channel = animation.channels.front();

    SECTION("different sample counts")
    {
        channel.values.pop_back();
    }
    SECTION("non-increasing timestamps")
    {
        channel.timestamps.back() = channel.timestamps.front();
    }
    SECTION("non-finite timestamp")
    {
        channel.timestamps.back() = std::numeric_limits<float>::infinity();
    }
    SECTION("zero quaternion")
    {
        channel.values.back() = Quaternion{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    }

    REQUIRE_THROWS_AS(fire_engine::detail::validateAnimation(animation), std::invalid_argument);
}

TEST_CASE("Animation binding validation accepts shared target-independent channels")
{
    const std::vector animations{makeAnimation()};
    Scene scene;
    scene.addRoot("first animator").component(makeAnimator());
    scene.addRoot("second animator").component(makeAnimator());

    REQUIRE_NOTHROW(fire_engine::detail::validateAnimationBindings(scene, animations));
}

TEST_CASE("Animation binding validation rejects dangling animator references")
{
    const std::vector animations{makeAnimation()};
    Scene scene;
    Animator animator = makeAnimator();

    SECTION("missing animation")
    {
        animator.animation = AnimationId{.value = 1};
    }
    SECTION("missing channel")
    {
        animator.channel = AnimationChannelId{.value = 1};
    }
    SECTION("invalid playback time")
    {
        animator.playbackTime = std::numeric_limits<float>::infinity();
    }
    SECTION("unsupported target path")
    {
        animator.targetPath = static_cast<AnimationTargetPath>(255);
    }
    scene.addRoot("animator").component(animator);

    REQUIRE_THROWS_AS(fire_engine::detail::validateAnimationBindings(scene, animations),
                      std::invalid_argument);
}
