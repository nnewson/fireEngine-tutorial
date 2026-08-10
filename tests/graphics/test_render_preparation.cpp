#include "fire_engine/graphics/render_preparation.hpp"

#include "fire_engine/graphics/render_assets.hpp"
#include "fire_engine/scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
using fire_engine::Color4;
using fire_engine::ImageData;
using fire_engine::Material;
using fire_engine::Mesh;
using fire_engine::RenderAssets;
using fire_engine::RenderObject;
using fire_engine::RenderObjectId;
using fire_engine::RenderPreparation;
using fire_engine::Scene;
using fire_engine::SceneNode;
using fire_engine::Texture;
using fire_engine::TextureFilter;
using fire_engine::TextureWrap;
using fire_engine::Vec3;
using fire_engine::Vertex;

Mesh makeTriangle()
{
    return Mesh{
        .vertices =
            {
                Vertex{
                    .position = Vec3{.x = 0.0f, .y = -0.5f, .z = 0.0f},
                    .color = Color4{.r = 1.0f, .a = 1.0f},
                    .textureCoordinate = {},
                },
                Vertex{
                    .position = Vec3{.x = 0.5f, .y = 0.5f, .z = 0.0f},
                    .color = Color4{.g = 1.0f, .a = 1.0f},
                    .textureCoordinate = {},
                },
                Vertex{
                    .position = Vec3{.x = -0.5f, .y = 0.5f, .z = 0.0f},
                    .color = Color4{.b = 1.0f, .a = 1.0f},
                    .textureCoordinate = {},
                },
            },
        .indices = {0, 1, 2},
    };
}

Texture makeTexture(fire_engine::ImageId image)
{
    return {
        .image = image,
        .minFilter = TextureFilter::eLinear,
        .magFilter = TextureFilter::eLinear,
        .wrapU = TextureWrap::eRepeat,
        .wrapV = TextureWrap::eRepeat,
    };
}
} // namespace

TEST_CASE("Color4 exposes color-domain component names")
{
    const Color4 color{.r = 0.1f, .g = 0.2f, .b = 0.3f, .a = 0.4f};

    REQUIRE(color.r == 0.1f);
    REQUIRE(color.g == 0.2f);
    REQUIRE(color.b == 0.3f);
    REQUIRE(color.a == 0.4f);
}

TEST_CASE("Render preparation shares mesh and material resources")
{
    RenderAssets assets;
    Scene scene;
    const auto mesh = assets.addMesh(makeTriangle());
    const auto material = assets.addMaterial(Material{});
    const auto first = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});
    const auto second = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});
    const auto unusedMesh = assets.addMesh(makeTriangle());
    const auto unusedMaterial = assets.addMaterial(Material{});
    static_cast<void>(
        assets.addRenderObject(RenderObject{.mesh = unusedMesh, .material = unusedMaterial}));

    auto root = std::make_unique<SceneNode>("first");
    root->component(first);
    scene.addRoot(std::move(root));

    auto otherRoot = std::make_unique<SceneNode>("second");
    otherRoot->component(second);
    scene.addRoot(std::move(otherRoot));

    RenderPreparation preparation;
    const auto& plan = preparation.build(assets, scene.buildDrawItems());

    REQUIRE(plan.meshes.size() == 1);
    REQUIRE(plan.materials.size() == 1);
    REQUIRE(plan.renderObjects.size() == 2);
    REQUIRE(plan.assetRevision == assets.revision());
}

TEST_CASE("Render preparation extracts shared image and texture dependencies")
{
    RenderAssets assets;
    const auto image =
        assets.addImage(ImageData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
    const auto firstTexture = assets.addTexture(makeTexture(image));
    const auto secondTexture = assets.addTexture(makeTexture(image));
    const auto mesh = assets.addMesh(makeTriangle());
    const auto firstMaterial = assets.addMaterial(Material{
        .baseColor = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f},
        .baseColorTexture = firstTexture,
    });
    const auto secondMaterial = assets.addMaterial(Material{
        .baseColor = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f},
        .baseColorTexture = secondTexture,
    });
    const auto firstObject =
        assets.addRenderObject(RenderObject{.mesh = mesh, .material = firstMaterial});
    const auto secondObject =
        assets.addRenderObject(RenderObject{.mesh = mesh, .material = secondMaterial});

    Scene scene;
    SceneNode& firstNode = scene.addRoot("first");
    firstNode.component(firstObject);
    SceneNode& secondNode = scene.addRoot("second");
    secondNode.component(secondObject);

    RenderPreparation preparation;
    const auto& plan = preparation.build(assets, scene.buildDrawItems());

    REQUIRE(plan.images == std::vector{image});
    REQUIRE(plan.textures == std::vector{firstTexture, secondTexture});
    REQUIRE(plan.materials == std::vector{firstMaterial, secondMaterial});
}

TEST_CASE("Render preparation rejects incomplete mesh data")
{
    SECTION("empty vertices")
    {
        RenderAssets assets;
        Scene scene;
        const auto mesh = assets.addMesh(Mesh{.vertices = {}, .indices = {0, 1, 2}});
        const auto material = assets.addMaterial(Material{});
        static_cast<void>(assets.addRenderObject(RenderObject{.mesh = mesh, .material = material}));

        RenderPreparation preparation;
        REQUIRE_THROWS_AS(preparation.build(assets, scene.buildDrawItems()), std::invalid_argument);
    }

    SECTION("out-of-range index")
    {
        RenderAssets assets;
        Scene scene;
        Mesh triangle = makeTriangle();
        triangle.indices[2] = 3;
        const auto mesh = assets.addMesh(std::move(triangle));
        const auto material = assets.addMaterial(Material{});
        static_cast<void>(assets.addRenderObject(RenderObject{.mesh = mesh, .material = material}));

        RenderPreparation preparation;
        REQUIRE_THROWS_AS(preparation.build(assets, scene.buildDrawItems()), std::invalid_argument);
    }
}

TEST_CASE("Render preparation rejects dangling references")
{
    SECTION("render object material")
    {
        RenderAssets assets;
        Scene scene;
        const auto mesh = assets.addMesh(makeTriangle());
        static_cast<void>(assets.addRenderObject(RenderObject{.mesh = mesh, .material = {}}));

        RenderPreparation preparation;
        REQUIRE_THROWS_AS(preparation.build(assets, scene.buildDrawItems()), std::invalid_argument);
    }

    SECTION("scene node render object")
    {
        RenderAssets assets;
        Scene scene;
        const auto mesh = assets.addMesh(makeTriangle());
        const auto material = assets.addMaterial(Material{});
        static_cast<void>(assets.addRenderObject(RenderObject{.mesh = mesh, .material = material}));

        auto root = std::make_unique<SceneNode>("dangling");
        root->component(RenderObjectId{.value = 1});
        scene.addRoot(std::move(root));

        RenderPreparation preparation;
        REQUIRE_THROWS_AS(preparation.build(assets, scene.buildDrawItems()), std::invalid_argument);
    }
}

TEST_CASE("Render preparation caches assets and transform-independent dependencies")
{
    RenderAssets assets;
    const auto mesh = assets.addMesh(makeTriangle());
    const auto material = assets.addMaterial(Material{});
    const auto object = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});

    Scene scene;
    SceneNode& root = scene.addRoot("triangle");
    root.component(object);
    scene.updateWorldTransforms();

    RenderPreparation preparation;
    const auto firstDrawList = scene.buildDrawItems();
    const auto& firstPlan = preparation.build(assets, firstDrawList);
    REQUIRE(preparation.generation() == 1);

    root.localTransform({
        .translation = {.x = 2.0f, .y = 0.0f, .z = 0.0f},
        .rotation = {},
        .scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f},
    });
    scene.updateWorldTransforms();
    const auto movedDrawList = scene.buildDrawItems();
    const auto& reusedPlan = preparation.build(assets, movedDrawList);

    REQUIRE(movedDrawList.dependencyHash == firstDrawList.dependencyHash);
    REQUIRE(&reusedPlan == &firstPlan);
    REQUIRE(preparation.generation() == 1);

    SceneNode& second = scene.addRoot("second triangle");
    second.component(object);
    auto expandedDrawList = scene.buildDrawItems();
    // Simulate a hash collision: the exact dependency sequence must still
    // distinguish one instance from two.
    expandedDrawList.dependencyHash = firstDrawList.dependencyHash;
    static_cast<void>(preparation.build(assets, expandedDrawList));
    REQUIRE(preparation.generation() == 2);

    static_cast<void>(assets.addMaterial(Material{}));
    static_cast<void>(preparation.build(assets, expandedDrawList));
    REQUIRE(preparation.generation() == 3);
}

TEST_CASE("Render asset revisions are independent of scene hierarchy")
{
    RenderAssets assets;
    Scene scene;
    const std::size_t initialRevision = assets.revision();

    scene.addRoot("root");
    REQUIRE(assets.revision() == initialRevision);

    static_cast<void>(assets.addMesh(makeTriangle()));
    REQUIRE(assets.revision() == initialRevision + 1);
}

TEST_CASE("Image and texture additions invalidate render preparation")
{
    RenderAssets assets;
    const auto mesh = assets.addMesh(makeTriangle());
    const auto material = assets.addMaterial(Material{});
    const auto object = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});
    Scene scene;
    scene.addRoot("triangle").component(object);

    RenderPreparation preparation;
    static_cast<void>(preparation.build(assets, scene.buildDrawItems()));
    REQUIRE(preparation.generation() == 1);

    const auto image =
        assets.addImage(ImageData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
    static_cast<void>(preparation.build(assets, scene.buildDrawItems()));
    REQUIRE(preparation.generation() == 2);

    static_cast<void>(assets.addTexture(makeTexture(image)));
    static_cast<void>(preparation.build(assets, scene.buildDrawItems()));
    REQUIRE(preparation.generation() == 3);
}
