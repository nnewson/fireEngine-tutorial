#include "procedural_shadow_receiver.hpp"
#include "shadow_caster_replacement.hpp"

#include <fire_engine/animation/animation_playback.hpp>
#include <fire_engine/content/detail/scene_content_validation.hpp>
#include <fire_engine/gltf/gltf_loader.hpp>
#include <fire_engine/graphics/render_preparation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <variant>
#include <vector>

namespace
{
namespace tutorial = fire_engine::tutorial;

using fire_engine::Animation;
using fire_engine::Animator;
using fire_engine::DrawItem;
using fire_engine::GltfLoader;
using fire_engine::ImageId;
using fire_engine::Material;
using fire_engine::MaterialId;
using fire_engine::Mesh;
using fire_engine::MeshId;
using fire_engine::RenderObject;
using fire_engine::RenderObjectId;
using fire_engine::RenderPreparation;
using fire_engine::RenderPreparationPlan;
using fire_engine::Scene;
using fire_engine::SceneContent;
using fire_engine::SceneDrawList;
using fire_engine::SceneDrawListArena;
using fire_engine::SceneNode;
using fire_engine::SceneNodeId;
using fire_engine::SceneNodeRef;
using fire_engine::TextureId;
using fire_engine::Vertex;

SceneContent loadCube()
{
    return GltfLoader{}.load(std::filesystem::path{FIRE_ENGINE_TEST_ASSET_DIRECTORY} /
                             "AnimatedCube" / "AnimatedCube.gltf");
}

SceneNode& cubeNode(SceneContent& content)
{
    REQUIRE(content.scene.roots().front()->children().size() == 1);
    return *content.scene.roots().front()->children().front();
}

std::vector<RenderObjectId> drawDependencies(const Scene& scene)
{
    SceneDrawListArena arena;
    const SceneDrawList draws = scene.buildDrawItems(arena);
    std::vector<RenderObjectId> result;
    for (const DrawItem& draw : draws.drawItems)
    {
        result.push_back(draw.renderObject);
    }
    return result;
}

void requireMesh(const Mesh& actual, const Mesh& expected)
{
    REQUIRE(actual.vertexLayout == expected.vertexLayout);
    REQUIRE(actual.indices == expected.indices);
    REQUIRE(actual.vertices.size() == expected.vertices.size());
    for (std::size_t index = 0; index < actual.vertices.size(); ++index)
    {
        REQUIRE(actual.vertices[index].position == expected.vertices[index].position);
        REQUIRE(actual.vertices[index].color == expected.vertices[index].color);
        REQUIRE(actual.vertices[index].textureCoordinate ==
                expected.vertices[index].textureCoordinate);
    }
}

void requireNode(const SceneNode& actual, const SceneNode& unchanged,
                 const tutorial::ShadowCasterReplacement& replacement)
{
    REQUIRE(actual.id() == unchanged.id());
    REQUIRE(actual.name() == unchanged.name());
    REQUIRE(actual.localTransform() == unchanged.localTransform());
    REQUIRE(actual.worldTransform() == unchanged.worldTransform());
    REQUIRE(actual.component().index() == unchanged.component().index());
    if (const auto* animator = std::get_if<Animator>(&unchanged.component()))
    {
        const Animator& actualAnimator = std::get<Animator>(actual.component());
        REQUIRE(actualAnimator.animation == animator->animation);
        REQUIRE(actualAnimator.channel == animator->channel);
        REQUIRE(actualAnimator.targetPath == animator->targetPath);
        REQUIRE(actualAnimator.playbackTime == animator->playbackTime);
        REQUIRE(actualAnimator.looping == animator->looping);
    }
    else if (const auto* object = std::get_if<RenderObjectId>(&unchanged.component()))
    {
        REQUIRE(std::get<RenderObjectId>(actual.component()) ==
                (actual.id() == replacement.node ? replacement.replacementObject : *object));
    }
    REQUIRE(actual.children().size() == unchanged.children().size());
    for (std::size_t index = 0; index < actual.children().size(); ++index)
    {
        requireNode(*actual.children()[index], *unchanged.children()[index], replacement);
    }
}

void requireHierarchy(const Scene& actual, const Scene& unchanged,
                      const tutorial::ShadowCasterReplacement& replacement)
{
    REQUIRE(actual.roots().size() == unchanged.roots().size());
    for (std::size_t index = 0; index < actual.roots().size(); ++index)
    {
        requireNode(*actual.roots()[index], *unchanged.roots()[index], replacement);
    }
}
} // namespace

TEST_CASE("Shadow caster replacement changes geometry and removes all prepared image dependencies")
{
    SceneContent content = loadCube();
    SceneContent unchanged = loadCube();
    const RenderObjectId receiver = tutorial::addProceduralShadowReceiver(content);
    REQUIRE(tutorial::addProceduralShadowReceiver(unchanged) == receiver);
    // Match the future replacement point: two fixed-step presentations, not a reset clip.
    for (int frame = 0; frame < 2; ++frame)
    {
        advanceAnimations(content.scene, content.animations, 0.8f);
        advanceAnimations(unchanged.scene, unchanged.animations, 0.8f);
    }
    content.scene.updateWorldTransforms();
    unchanged.scene.updateWorldTransforms();

    SceneNode& casterNode = cubeNode(content);
    const SceneNodeId casterNodeId = casterNode.id().value_or(SceneNodeId{});
    REQUIRE(casterNodeId.valid());
    const RenderObjectId caster = std::get<RenderObjectId>(casterNode.component());
    const RenderObject original = content.assets.renderObjects().at(caster.value);
    const RenderObject receiverObject = content.assets.renderObjects().at(receiver.value);
    const Material originalMaterial = content.assets.materials().at(original.material.value);
    REQUIRE(originalMaterial.baseColorTexture.has_value());
    const TextureId originalTexture = originalMaterial.baseColorTexture.value_or(TextureId{});
    const ImageId originalImage = content.assets.textures().at(originalTexture.value).image;
    const std::size_t originalRevision = content.assets.revision();
    REQUIRE(drawDependencies(content.scene) == std::vector<RenderObjectId>{caster, receiver});

    RenderPreparation preparation;
    SceneDrawListArena arena;
    {
        const SceneDrawList draws = content.scene.buildDrawItems(arena);
        const RenderPreparationPlan& plan = preparation.build(content.assets, draws, {});
        REQUIRE(plan.images == std::vector<ImageId>{originalImage});
        REQUIRE(plan.textures == std::vector<TextureId>{originalTexture});
        REQUIRE(preparation.generation() == 1);
    }

    const tutorial::ShadowCasterReplacement replacement = tutorial::replaceShadowCaster(content);
    REQUIRE(replacement.node == casterNodeId);
    REQUIRE(content.scene.findNode(replacement.node)
                .transform([](SceneNodeRef node) { return &node.get(); })
                .value_or(nullptr) == &casterNode);
    REQUIRE(replacement.originalObject == caster);
    REQUIRE(replacement.replacementObject ==
            RenderObjectId{.value = unchanged.assets.renderObjects().size()});
    REQUIRE(content.assets.revision() == originalRevision + 3);
    REQUIRE(content.assets.meshes().size() == unchanged.assets.meshes().size() + 1);
    REQUIRE(content.assets.materials().size() == unchanged.assets.materials().size() + 1);
    REQUIRE(content.assets.renderObjects().size() == unchanged.assets.renderObjects().size() + 1);
    REQUIRE(content.assets.images().size() == unchanged.assets.images().size());
    REQUIRE(content.assets.textures().size() == unchanged.assets.textures().size());

    const RenderObject appended =
        content.assets.renderObjects().at(replacement.replacementObject.value);
    REQUIRE(appended.mesh == MeshId{.value = unchanged.assets.meshes().size()});
    REQUIRE(appended.material == MaterialId{.value = unchanged.assets.materials().size()});
    REQUIRE(appended.castsShadow);
    const Material& material = content.assets.materials().at(appended.material.value);
    REQUIRE(material.baseColor == originalMaterial.baseColor);
    Mesh expectedMesh = unchanged.assets.meshes().at(original.mesh.value);
    for (Vertex& vertex : expectedMesh.vertices)
    {
        vertex.position.x *= 0.75f;
    }
    requireMesh(content.assets.meshes().at(appended.mesh.value), expectedMesh);

    // Catalog append must not alter the original caster or the receive-only plane.
    for (std::size_t index = 0; index < unchanged.assets.meshes().size(); ++index)
    {
        requireMesh(content.assets.meshes()[index], unchanged.assets.meshes()[index]);
    }
    for (std::size_t index = 0; index < unchanged.assets.materials().size(); ++index)
    {
        REQUIRE(content.assets.materials()[index].baseColor ==
                unchanged.assets.materials()[index].baseColor);
        REQUIRE(content.assets.materials()[index].baseColorTexture ==
                unchanged.assets.materials()[index].baseColorTexture);
    }
    for (std::size_t index = 0; index < unchanged.assets.renderObjects().size(); ++index)
    {
        REQUIRE(content.assets.renderObjects()[index].mesh ==
                unchanged.assets.renderObjects()[index].mesh);
        REQUIRE(content.assets.renderObjects()[index].material ==
                unchanged.assets.renderObjects()[index].material);
        REQUIRE(content.assets.renderObjects()[index].castsShadow ==
                unchanged.assets.renderObjects()[index].castsShadow);
    }
    requireHierarchy(content.scene, unchanged.scene, replacement);
    REQUIRE(content.animations.size() == unchanged.animations.size());
    for (std::size_t index = 0; index < content.animations.size(); ++index)
    {
        const Animation& actual = content.animations[index];
        const Animation& expected = unchanged.animations[index];
        REQUIRE(actual.name == expected.name);
        REQUIRE(actual.channels.size() == expected.channels.size());
        for (std::size_t channel = 0; channel < actual.channels.size(); ++channel)
        {
            REQUIRE(actual.channels[channel].timestamps == expected.channels[channel].timestamps);
            REQUIRE(actual.channels[channel].values == expected.channels[channel].values);
        }
    }

    content.scene.updateWorldTransforms();
    REQUIRE(drawDependencies(content.scene) ==
            std::vector<RenderObjectId>{replacement.replacementObject, receiver});
    {
        const SceneDrawList draws = content.scene.buildDrawItems(arena);
        const RenderPreparationPlan& plan = preparation.build(content.assets, draws, {});
        REQUIRE(preparation.generation() == 2);
        REQUIRE(plan.assetRevision == originalRevision + 3);
        // Preparation sorts distinct resources by ID, unlike the scene's draw order.
        REQUIRE(plan.meshes == std::vector<MeshId>{receiverObject.mesh, appended.mesh});
        REQUIRE(plan.materials ==
                std::vector<MaterialId>{receiverObject.material, appended.material});
        REQUIRE(plan.renderObjects.size() == 2);
        REQUIRE(plan.renderObjects[0].id == receiver);
        REQUIRE_FALSE(plan.renderObjects[0].castsShadow);
        REQUIRE(plan.renderObjects[1].id == replacement.replacementObject);
        REQUIRE(plan.renderObjects[1].castsShadow);
        // This is the load-bearing control: unused textured assets stay in the catalog,
        // but the replacement plan cannot introduce an upload fence that masks prepare().
        REQUIRE(plan.images.empty());
        REQUIRE(plan.textures.empty());
    }

    REQUIRE_NOTHROW(fire_engine::detail::validateSceneContent(content));
    advanceAnimations(content.scene, content.animations, 0.8f);
    advanceAnimations(unchanged.scene, unchanged.animations, 0.8f);
    content.scene.updateWorldTransforms();
    unchanged.scene.updateWorldTransforms();
    requireHierarchy(content.scene, unchanged.scene, replacement);
}

TEST_CASE("Shadow caster replacement selects the active instance rather than catalog position")
{
    SceneContent content = loadCube();
    const RenderObjectId receiver = tutorial::addProceduralShadowReceiver(content);
    SceneNode& node = cubeNode(content);
    const RenderObject original = content.assets.renderObjects().front();
    Material tinted = content.assets.materials().at(original.material.value);
    tinted.baseColor = {.r = 0.25f, .g = 0.5f, .b = 0.75f, .a = 0.5f};
    const MaterialId tintedMaterial = content.assets.addMaterial(tinted);
    const RenderObjectId activeCaster = content.assets.addRenderObject({
        .mesh = original.mesh,
        .material = tintedMaterial,
        .castsShadow = true,
    });
    node.component(activeCaster);
    const tutorial::ShadowCasterReplacement first = tutorial::replaceShadowCaster(content);
    REQUIRE(first.originalObject == activeCaster);
    const RenderObject firstObject =
        content.assets.renderObjects().at(first.replacementObject.value);
    REQUIRE(content.assets.materials().at(firstObject.material.value).baseColor ==
            tinted.baseColor);
    REQUIRE_FALSE(
        content.assets.materials().at(firstObject.material.value).baseColorTexture.has_value());

    // Both old casting descriptions remain unused; neither is another casting instance.
    const tutorial::ShadowCasterReplacement second = tutorial::replaceShadowCaster(content);
    REQUIRE(second.node == first.node);
    REQUIRE(second.originalObject == first.replacementObject);
    REQUIRE(second.replacementObject != first.replacementObject);
    REQUIRE(drawDependencies(content.scene) ==
            std::vector<RenderObjectId>{second.replacementObject, receiver});
}

TEST_CASE("Shadow caster replacement rejects a nonunique casting instance before mutation")
{
    SceneContent content = loadCube();
    const RenderObjectId receiver = tutorial::addProceduralShadowReceiver(content);
    SceneNode& node = cubeNode(content);
    const RenderObjectId caster = std::get<RenderObjectId>(node.component());
    SECTION("No caster")
    {
        node.component(receiver);
    }
    SECTION("Two nodes share one casting object")
    {
        content.scene.addRoot("Second caster instance").component(caster);
    }
    SECTION("Two nodes reference different casting objects")
    {
        const RenderObjectId other =
            content.assets.addRenderObject(content.assets.renderObjects().at(caster.value));
        content.scene.addRoot("Different caster").component(other);
    }
    const std::size_t revision = content.assets.revision();
    const auto dependencies = drawDependencies(content.scene);
    REQUIRE_THROWS_WITH(tutorial::replaceShadowCaster(content),
                        "Shadow replacement fixture requires exactly one casting scene node");
    REQUIRE(content.assets.revision() == revision);
    REQUIRE(drawDependencies(content.scene) == dependencies);
}

TEST_CASE("Shadow caster replacement rejects missing asset references before mutation")
{
    SceneContent content = loadCube();
    static_cast<void>(tutorial::addProceduralShadowReceiver(content));
    SceneNode& node = cubeNode(content);
    RenderObject broken = content.assets.renderObjects().front();
    SECTION("Missing scene render object")
    {
        node.component(RenderObjectId{});
    }
    SECTION("Missing caster mesh")
    {
        broken.mesh = MeshId{};
        node.component(content.assets.addRenderObject(broken));
    }
    SECTION("Missing caster material")
    {
        broken.material = MaterialId{};
        node.component(content.assets.addRenderObject(broken));
    }
    const std::size_t revision = content.assets.revision();
    const auto dependencies = drawDependencies(content.scene);
    REQUIRE_THROWS_AS(tutorial::replaceShadowCaster(content), std::out_of_range);
    REQUIRE(content.assets.revision() == revision);
    REQUIRE(drawDependencies(content.scene) == dependencies);
}
