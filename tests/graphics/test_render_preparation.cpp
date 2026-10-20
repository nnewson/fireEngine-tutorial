#include "fire_engine/graphics/render_preparation.hpp"

#include "fire_engine/graphics/render_assets.hpp"
#include "fire_engine/scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
using fire_engine::Color4;
using fire_engine::ImageData;
using fire_engine::Material;
using fire_engine::Mesh;
using fire_engine::PipelineDescription;
using fire_engine::RenderAssets;
using fire_engine::RenderAssetsStamp;
using fire_engine::RenderObject;
using fire_engine::RenderObjectId;
using fire_engine::RenderPreparation;
using fire_engine::RenderPreparationPlan;
using fire_engine::Scene;
using fire_engine::SceneNode;
using fire_engine::SceneSnapshot;
using fire_engine::Texture;
using fire_engine::TextureFilter;
using fire_engine::TextureWrap;
using fire_engine::Vec3;
using fire_engine::Vertex;
using fire_engine::VertexLayoutKey;

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

/** @brief Returns a snapshot's ordered render-object references. */
std::vector<RenderObjectId> renderObjects(const SceneSnapshot& snapshot)
{
    std::vector<RenderObjectId> result;
    for (const fire_engine::DrawItem& drawItem : snapshot.drawItems())
    {
        result.push_back(drawItem.renderObject);
    }
    return result;
}

RenderAssets makeTriangleAssets(bool castsShadow)
{
    RenderAssets assets;
    const auto mesh = assets.addMesh(makeTriangle());
    const auto material = assets.addMaterial(Material{});
    static_cast<void>(assets.addRenderObject(
        RenderObject{.mesh = mesh, .material = material, .castsShadow = castsShadow}));
    return assets;
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

TEST_CASE("Render preparation preserves shadow participation across same-shaped replacement")
{
    const RenderObject defaultObject{};
    REQUIRE(defaultObject.castsShadow);

    RenderAssets assets = makeTriangleAssets(true);
    const std::size_t originalRevision = assets.revision();
    Scene scene;
    scene.addRoot("triangle").component(RenderObjectId{.value = 0});

    RenderPreparation preparation;
    const SceneSnapshot castingSnapshot = scene.buildSnapshot(assets);
    const RenderPreparationPlan& castingPlan =
        preparation.build(assets, castingSnapshot, PipelineDescription{});
    REQUIRE(preparation.generation() == 1);
    REQUIRE(castingPlan.renderObjects.size() == 1);
    REQUIRE(castingPlan.renderObjects.front().castsShadow);

    RenderAssets replacement = makeTriangleAssets(false);
    const fire_engine::RenderAssetsId replacementId = replacement.id();
    REQUIRE(replacement.revision() == originalRevision);
    assets = std::move(replacement);

    // The destination adopts the replacement's identity with its content. The
    // revision, numeric IDs and draw dependencies all coincide with the
    // prepared catalog's, so only the identity can invalidate the plan.
    REQUIRE(assets.id() == replacementId);
    REQUIRE(assets.revision() == originalRevision);
    const SceneSnapshot replacementSnapshot = scene.buildSnapshot(assets);
    REQUIRE(renderObjects(replacementSnapshot) == renderObjects(castingSnapshot));
    const RenderPreparationPlan& replacementPlan =
        preparation.build(assets, replacementSnapshot, PipelineDescription{});

    REQUIRE(preparation.generation() == 2);
    REQUIRE(replacementPlan.renderObjects.size() == 1);
    REQUIRE(replacementPlan.renderObjects.front().id == RenderObjectId{.value = 0});
    REQUIRE_FALSE(replacementPlan.renderObjects.front().castsShadow);
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
    const auto& plan =
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{});

    REQUIRE(plan.meshes.size() == 1);
    REQUIRE(plan.materials.size() == 1);
    REQUIRE(plan.renderObjects.size() == 2);
    REQUIRE(plan.assets == assets.stamp());
    REQUIRE(plan.pipeline == PipelineDescription{});
    REQUIRE(plan.renderObjects.front().vertexLayout ==
            VertexLayoutKey::ePositionColorTextureCoordinate);
}

TEST_CASE("Render preparation keys its cache by the selected pipeline")
{
    RenderAssets assets;
    Mesh incompatibleMesh = makeTriangle();
    const VertexLayoutKey alternateLayout = static_cast<VertexLayoutKey>(255);
    incompatibleMesh.vertexLayout = alternateLayout;
    const auto mesh = assets.addMesh(std::move(incompatibleMesh));
    const auto material = assets.addMaterial(Material{});
    const auto object = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});
    Scene scene;
    scene.addRoot("incompatible").component(object);

    RenderPreparation firstBuildPreparation;
    REQUIRE_THROWS_AS(
        firstBuildPreparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}),
        std::invalid_argument);

    RenderPreparation preparation;
    const PipelineDescription alternatePipeline{.vertexLayout = alternateLayout};
    const RenderPreparationPlan& firstPlan =
        preparation.build(assets, scene.buildSnapshot(assets), alternatePipeline);
    REQUIRE(firstPlan.pipeline == alternatePipeline);
    REQUIRE(preparation.generation() == 1);

    // If the pipeline description were absent from the cache key, this would
    // return firstPlan before reaching the incompatible-layout check.
    REQUIRE_THROWS_AS(preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}),
                      std::invalid_argument);
    REQUIRE(preparation.generation() == 1);
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
    const auto& plan =
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{});

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
        REQUIRE_THROWS_AS(
            preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}),
            std::invalid_argument);
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
        REQUIRE_THROWS_AS(
            preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}),
            std::invalid_argument);
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
        REQUIRE_THROWS_AS(
            preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}),
            std::invalid_argument);
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
        REQUIRE_THROWS_AS(
            preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}),
            std::invalid_argument);
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
    const SceneSnapshot firstSnapshot = scene.buildSnapshot(assets);
    const auto& firstPlan = preparation.build(assets, firstSnapshot, PipelineDescription{});
    REQUIRE(preparation.generation() == 1);

    root.localTransform({
        .translation = {.x = 2.0f, .y = 0.0f, .z = 0.0f},
        .rotation = {},
        .scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f},
    });
    scene.updateWorldTransforms();
    const SceneSnapshot movedSnapshot = scene.buildSnapshot(assets);
    const auto& reusedPlan = preparation.build(assets, movedSnapshot, PipelineDescription{});

    REQUIRE(movedSnapshot.drawItems().front().world != firstSnapshot.drawItems().front().world);
    REQUIRE(&reusedPlan == &firstPlan);
    REQUIRE(preparation.generation() == 1);

    SceneNode& second = scene.addRoot("second triangle");
    second.component(object);
    const SceneSnapshot expandedSnapshot = scene.buildSnapshot(assets);
    static_cast<void>(preparation.build(assets, expandedSnapshot, PipelineDescription{}));
    REQUIRE(preparation.generation() == 2);

    const RenderPreparationPlan beforeAssetMutation =
        preparation.build(assets, expandedSnapshot, PipelineDescription{});
    static_cast<void>(assets.addMaterial(Material{}));
    const RenderPreparationPlan& afterAssetMutation =
        preparation.build(assets, expandedSnapshot, PipelineDescription{});
    REQUIRE(preparation.generation() == 3);
    REQUIRE(afterAssetMutation.meshes == beforeAssetMutation.meshes);
    REQUIRE(afterAssetMutation.images == beforeAssetMutation.images);
    REQUIRE(afterAssetMutation.textures == beforeAssetMutation.textures);
    REQUIRE(afterAssetMutation.materials == beforeAssetMutation.materials);
    REQUIRE(afterAssetMutation.renderObjects.size() == beforeAssetMutation.renderObjects.size());
    REQUIRE(afterAssetMutation.renderObjects.front().id ==
            beforeAssetMutation.renderObjects.front().id);
    REQUIRE(afterAssetMutation.renderObjects.front().mesh ==
            beforeAssetMutation.renderObjects.front().mesh);
    REQUIRE(afterAssetMutation.renderObjects.front().material ==
            beforeAssetMutation.renderObjects.front().material);
    REQUIRE(afterAssetMutation.renderObjects.front().vertexLayout ==
            beforeAssetMutation.renderObjects.front().vertexLayout);
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
    static_cast<void>(
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}));
    REQUIRE(preparation.generation() == 1);

    const auto image =
        assets.addImage(ImageData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
    static_cast<void>(
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}));
    REQUIRE(preparation.generation() == 2);

    static_cast<void>(assets.addTexture(makeTexture(image)));
    static_cast<void>(
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}));
    REQUIRE(preparation.generation() == 3);
}

TEST_CASE("Render preparation never reuses a plan for a catalog built at a destroyed one's address")
{
    std::optional<RenderAssets> assets{std::in_place, makeTriangleAssets(true)};
    const RenderAssets* const address = &*assets;
    Scene scene;
    scene.addRoot("triangle").component(RenderObjectId{.value = 0});

    RenderPreparation preparation;
    REQUIRE(preparation.build(*assets, scene.buildSnapshot(*assets), PipelineDescription{})
                .renderObjects.front()
                .castsShadow);
    const std::size_t revision = assets->revision();

    // The same address, revision, numeric IDs and draws: only the identity differs.
    assets.reset();
    assets.emplace(makeTriangleAssets(false));
    REQUIRE(&*assets == address);
    REQUIRE(assets->revision() == revision);

    const RenderPreparationPlan& plan =
        preparation.build(*assets, scene.buildSnapshot(*assets), PipelineDescription{});
    REQUIRE(preparation.generation() == 2);
    REQUIRE(plan.assets == assets->stamp());
    REQUIRE_FALSE(plan.renderObjects.front().castsShadow);
}

TEST_CASE("Render preparation reuses a plan for a catalog moved with unchanged content")
{
    RenderAssets original = makeTriangleAssets(true);
    Scene scene;
    scene.addRoot("triangle").component(RenderObjectId{.value = 0});

    RenderPreparation preparation;
    const RenderPreparationPlan& first =
        preparation.build(original, scene.buildSnapshot(original), PipelineDescription{});
    const RenderAssetsStamp stamp = original.stamp();

    const RenderAssets moved{std::move(original)};
    const RenderPreparationPlan& reused =
        preparation.build(moved, scene.buildSnapshot(moved), PipelineDescription{});

    REQUIRE(preparation.generation() == 1);
    REQUIRE(&reused == &first);
    REQUIRE(reused.assets == stamp);
}

TEST_CASE("Render preparation never returns another catalog's plan after a rejected build")
{
    const RenderAssets first = makeTriangleAssets(true);
    const RenderAssets second = makeTriangleAssets(false);
    Scene scene;
    scene.addRoot("triangle").component(RenderObjectId{.value = 0});
    Scene dangling;
    dangling.addRoot("missing").component(RenderObjectId{.value = 5});

    RenderPreparation preparation;
    static_cast<void>(preparation.build(first, scene.buildSnapshot(first), PipelineDescription{}));

    // The second catalog validates, then its draws are rejected while planning.
    REQUIRE_THROWS_AS(
        preparation.build(second, dangling.buildSnapshot(second), PipelineDescription{}),
        std::invalid_argument);

    const RenderPreparationPlan& retry =
        preparation.build(second, scene.buildSnapshot(second), PipelineDescription{});
    REQUIRE(preparation.generation() == 2);
    REQUIRE(retry.assets == second.stamp());
    REQUIRE_FALSE(retry.renderObjects.front().castsShadow);
}

TEST_CASE("Render preparation never returns an earlier revision's plan after a rejected build")
{
    RenderAssets assets = makeTriangleAssets(true);
    Scene scene;
    scene.addRoot("triangle").component(RenderObjectId{.value = 0});
    Scene dangling;
    dangling.addRoot("missing").component(RenderObjectId{.value = 5});

    RenderPreparation preparation;
    static_cast<void>(
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{}));
    static_cast<void>(assets.addMaterial(Material{}));

    // The appended catalog validates, then its draws are rejected while planning.
    REQUIRE_THROWS_AS(
        preparation.build(assets, dangling.buildSnapshot(assets), PipelineDescription{}),
        std::invalid_argument);

    const RenderPreparationPlan& retry =
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{});
    REQUIRE(preparation.generation() == 2);
    REQUIRE(retry.assets == assets.stamp());
}

TEST_CASE("Render preparation keys its plan on the exact ordered dependencies")
{
    RenderAssets assets;
    const auto mesh = assets.addMesh(makeTriangle());
    const auto material = assets.addMaterial(Material{});
    const auto first = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});
    const auto second = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});
    Scene forward;
    forward.addRoot("first").component(first);
    forward.addRoot("second").component(second);
    Scene reversed;
    reversed.addRoot("second").component(second);
    reversed.addRoot("first").component(first);

    RenderPreparation preparation;
    static_cast<void>(
        preparation.build(assets, forward.buildSnapshot(assets), PipelineDescription{}));

    // The same length and the same distinct objects, in another order.
    const SceneSnapshot reorderedSnapshot = reversed.buildSnapshot(assets);
    REQUIRE(renderObjects(reorderedSnapshot) == std::vector<RenderObjectId>{second, first});
    static_cast<void>(preparation.build(assets, reorderedSnapshot, PipelineDescription{}));
    REQUIRE(preparation.generation() == 2);
}

TEST_CASE("Render preparation keeps no reference to the snapshot it was built from")
{
    RenderAssets assets;
    const auto mesh = assets.addMesh(makeTriangle());
    const auto material = assets.addMaterial(Material{});
    const auto first = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});
    const auto second = assets.addRenderObject(RenderObject{.mesh = mesh, .material = material});
    Scene scene;
    scene.addRoot("first").component(first);

    RenderPreparation preparation;
    SceneSnapshot snapshot = scene.buildSnapshot(assets);
    const RenderPreparationPlan& plan = preparation.build(assets, snapshot, PipelineDescription{});

    // Reuse the released storage for other draws. A key that still viewed it
    // would now describe the second object and miss below.
    std::vector<fire_engine::DrawItem> storage = std::move(snapshot).releaseStorage();
    storage.front().renderObject = second;

    const RenderPreparationPlan& reused =
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{});
    REQUIRE(preparation.generation() == 1);
    REQUIRE(&reused == &plan);
}

TEST_CASE("Render preparation rejects a snapshot from another catalog without changing its cache")
{
    const RenderAssets assets = makeTriangleAssets(true);
    const RenderAssets other = makeTriangleAssets(true);
    Scene scene;
    scene.addRoot("triangle").component(RenderObjectId{.value = 0});

    RenderPreparation preparation;
    const RenderPreparationPlan& plan =
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{});

    // Same entries, numeric IDs and revision: only the catalog differs.
    REQUIRE_THROWS_AS(preparation.build(assets, scene.buildSnapshot(other), PipelineDescription{}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(preparation.build(assets, SceneSnapshot{}, PipelineDescription{}),
                      std::invalid_argument);

    const RenderPreparationPlan& reused =
        preparation.build(assets, scene.buildSnapshot(assets), PipelineDescription{});
    REQUIRE(preparation.generation() == 1);
    REQUIRE(&reused == &plan);
}

TEST_CASE("Render preparation accepts a snapshot from an earlier revision of its catalog")
{
    RenderAssets assets = makeTriangleAssets(true);
    Scene scene;
    scene.addRoot("triangle").component(RenderObjectId{.value = 0});
    const SceneSnapshot earlier = scene.buildSnapshot(assets);
    static_cast<void>(assets.addMaterial(Material{}));

    RenderPreparation preparation;
    const RenderPreparationPlan& plan = preparation.build(assets, earlier, PipelineDescription{});
    // The plan describes the catalog as it is now; the older draws are covered by it.
    REQUIRE(plan.assets == assets.stamp());
    REQUIRE(plan.renderObjects.size() == 1);
}
