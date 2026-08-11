#include "fire_engine/content/detail/scene_content_validation.hpp"

#include "fire_engine/content/scene_content.hpp"
#include "fire_engine/scene/animator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

TEST_CASE("Scene content validation checks asset and animation composition")
{
    fire_engine::SceneContent content;

    SECTION("invalid asset descriptions")
    {
        const fire_engine::MeshId mesh = content.assets.addMesh({.vertices = {}, .indices = {}});
        const fire_engine::MaterialId material = content.assets.addMaterial({});
        const fire_engine::RenderObjectId renderObject =
            content.assets.addRenderObject({.mesh = mesh, .material = material});
        content.scene.addRoot("invalid mesh").component(renderObject);
    }
    SECTION("dangling animation bindings")
    {
        content.scene.addRoot("invalid animator")
            .component(fire_engine::Animator{
                .animation = fire_engine::AnimationId{.value = 0},
                .channel = fire_engine::AnimationChannelId{.value = 0},
                .targetPath = fire_engine::AnimationTargetPath::eRotation,
                .playbackTime = 0.0f,
                .looping = true,
            });
    }

    REQUIRE_THROWS_AS(fire_engine::detail::validateSceneContent(content), std::invalid_argument);
}
