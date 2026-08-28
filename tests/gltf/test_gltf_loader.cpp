#include "fire_engine/gltf/gltf_loader.hpp"

#include "fire_engine/content/detail/scene_content_validation.hpp"
#include "fire_engine/scene/animator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string_view>
#include <variant>

namespace
{
[[nodiscard]] std::filesystem::path animatedCubePath()
{
    return std::filesystem::path{FIRE_ENGINE_TEST_ASSET_DIRECTORY} / "AnimatedCube" /
           "AnimatedCube.gltf";
}

[[nodiscard]] std::filesystem::path fixturePath(std::string_view name)
{
    return std::filesystem::path{FIRE_ENGINE_TEST_FIXTURE_DIRECTORY} / "gltf" / name;
}
} // namespace

TEST_CASE("The minimal glTF loader imports a selected scene hierarchy")
{
    fire_engine::SceneContent loaded =
        fire_engine::GltfLoader{}.load(fixturePath("hierarchy.gltf"));

    REQUIRE(loaded.assets.renderObjects().empty());
    REQUIRE(loaded.animations.empty());
    REQUIRE(loaded.scene.roots().size() == 1);
    const fire_engine::SceneNode& parent = *loaded.scene.roots().front();
    REQUIRE(parent.name() == "parent");
    REQUIRE(parent.localTransform().translation ==
            fire_engine::Vec3{.x = 1.0f, .y = 2.0f, .z = 3.0f});
    REQUIRE(parent.children().size() == 1);
    REQUIRE(parent.children().front()->name() == "child");
    REQUIRE(parent.children().front()->localTransform().scale ==
            fire_engine::Vec3{.x = 2.0f, .y = 3.0f, .z = 4.0f});
}

TEST_CASE("The minimal glTF loader imports AnimatedCube CPU descriptions")
{
    fire_engine::SceneContent loaded = fire_engine::GltfLoader{}.load(animatedCubePath());

    REQUIRE(loaded.assets.meshes().size() == 1);
    REQUIRE(loaded.assets.meshes().front().vertices.size() == 36);
    REQUIRE(loaded.assets.meshes().front().indices.size() == 36);
    REQUIRE(loaded.assets.images().size() == 2);
    REQUIRE(loaded.assets.images().front().width == 512);
    REQUIRE(loaded.assets.images().front().height == 512);
    REQUIRE(loaded.assets.images().front().pixels.size() == 512 * 512 * 4);
    REQUIRE(loaded.assets.textures().size() == 2);
    REQUIRE(loaded.assets.materials().size() == 1);
    REQUIRE(loaded.assets.materials().front().baseColorTexture ==
            fire_engine::TextureId{.value = 0});
    REQUIRE(loaded.assets.renderObjects().size() == 1);

    REQUIRE_NOTHROW(fire_engine::detail::validateSceneContent(loaded));
}

TEST_CASE("The minimal glTF loader preserves hierarchy and reusable rotation data")
{
    fire_engine::SceneContent loaded = fire_engine::GltfLoader{}.load(animatedCubePath());

    REQUIRE(loaded.scene.roots().size() == 1);
    const fire_engine::SceneNode& root = *loaded.scene.roots().front();
    REQUIRE(root.name() == "AnimatedCube");
    REQUIRE(std::holds_alternative<fire_engine::Animator>(root.component()));
    REQUIRE(root.children().size() == 1);
    REQUIRE(
        std::holds_alternative<fire_engine::RenderObjectId>(root.children().front()->component()));
    fire_engine::SceneDrawListArena drawListArena;
    REQUIRE(loaded.scene.buildDrawItems(drawListArena).drawItems.size() == 1);

    REQUIRE(loaded.animations.size() == 1);
    REQUIRE(loaded.animations.front().name == "animation_AnimatedCube");
    REQUIRE(loaded.animations.front().channels.size() == 1);
    const fire_engine::AnimationChannel& channel = loaded.animations.front().channels.front();
    REQUIRE(channel.timestamps == std::vector{0.0f, 1.0f, 2.0f});
    REQUIRE(channel.values.size() == channel.timestamps.size());
    REQUIRE_NOTHROW(fire_engine::detail::validateSceneContent(loaded));
}

TEST_CASE("The minimal glTF loader reports missing input files")
{
    REQUIRE_THROWS_WITH(fire_engine::GltfLoader{}.load("missing.gltf"),
                        Catch::Matchers::ContainsSubstring("Could not read glTF"));
}

TEST_CASE("The minimal glTF loader identifies unsupported required data")
{
    REQUIRE_THROWS_WITH(
        fire_engine::GltfLoader{}.load(fixturePath("required_extension.gltf")),
        Catch::Matchers::ContainsSubstring("required extension 'KHR_materials_transmission'"));
    REQUIRE_THROWS_WITH(
        fire_engine::GltfLoader{}.load(fixturePath("non_triangle.gltf")),
        Catch::Matchers::ContainsSubstring("mesh primitives must use triangle-list mode"));
    REQUIRE_THROWS_WITH(fire_engine::GltfLoader{}.load(fixturePath("non_indexed.gltf")),
                        Catch::Matchers::ContainsSubstring("mesh primitives must be indexed"));
    REQUIRE_THROWS_WITH(
        fire_engine::GltfLoader{}.load(fixturePath("missing_texture_coordinate.gltf")),
        Catch::Matchers::ContainsSubstring("require POSITION and TEXCOORD_0"));
    REQUIRE_THROWS_WITH(
        fire_engine::GltfLoader{}.load(fixturePath("non_linear_animation.gltf")),
        Catch::Matchers::ContainsSubstring("animation interpolation must be LINEAR"));
    REQUIRE_THROWS_WITH(
        fire_engine::GltfLoader{}.load(fixturePath("non_rotation_animation.gltf")),
        Catch::Matchers::ContainsSubstring("only rotation animation channels are supported"));
}
