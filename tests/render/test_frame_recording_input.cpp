#include <fire_engine/render/detail/frame_recording_input.hpp>

#include <fire_engine/math/transform.hpp>
#include <fire_engine/render/detail/compiled_resource_graph.hpp>
#include <fire_engine/scene/scene_draw_list.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace
{
template <typename Handle, typename NativeHandle>
[[nodiscard]] Handle fakeHandle(std::uintptr_t value)
{
    if constexpr (std::is_pointer_v<NativeHandle>)
    {
        return Handle{reinterpret_cast<NativeHandle>(value)}; // NOLINT(performance-no-int-to-ptr)
    }
    else
    {
        return Handle{static_cast<NativeHandle>(value)};
    }
}

[[nodiscard]] fire_engine::detail::CompiledRenderObject
requireCompiledObject(const fire_engine::detail::CompiledResourcesView& resources,
                      fire_engine::RenderObjectId id)
{
    const auto object = resources.find(id);
    if (!object.has_value())
    {
        throw std::runtime_error{"Test fixture refers to an uncompiled render object"};
    }
    return object.value();
}

[[nodiscard]] fire_engine::detail::CompiledRenderObject
compiledRenderObject(std::uintptr_t firstHandle, fire_engine::Color4 baseColor,
                     bool castsShadow = true)
{
    return {
        .geometry =
            {
                .vertexBuffer = fakeHandle<vk::Buffer, VkBuffer>(firstHandle),
                .indexBuffer = fakeHandle<vk::Buffer, VkBuffer>(firstHandle + 1),
                .indexCount = static_cast<std::uint32_t>(firstHandle),
                .vertexLayout = fire_engine::VertexLayoutKey::ePositionColorTextureCoordinate,
            },
        .forwardMaterial =
            {
                .sampler = fakeHandle<vk::Sampler, VkSampler>(firstHandle + 2),
                .imageView = fakeHandle<vk::ImageView, VkImageView>(firstHandle + 3),
                .baseColor = baseColor,
            },
        .castsShadow = castsShadow,
    };
}

[[nodiscard]] fire_engine::detail::FrameUniforms frameUniforms()
{
    return {
        .viewProjection = fire_engine::Mat4::identity(),
        .lightViewProjection =
            fire_engine::Transform{.translation = {.x = 4.0f, .y = 5.0f, .z = 6.0f}}.matrix(),
    };
}

[[nodiscard]] fire_engine::detail::ShadowRecordingState shadowRecordingState()
{
    return {
        .pipeline = fakeHandle<vk::Pipeline, VkPipeline>(10),
        .pipelineLayout = fakeHandle<vk::PipelineLayout, VkPipelineLayout>(11),
        .frameUniformBuffer = fakeHandle<vk::Buffer, VkBuffer>(12),
        .depthAttachmentFormat = vk::Format::eD32Sfloat,
        .vertexLayout = fire_engine::VertexLayoutKey::ePositionColorTextureCoordinate,
    };
}

[[nodiscard]] fire_engine::detail::ForwardRecordingState forwardRecordingState()
{
    return {
        .pipeline = fakeHandle<vk::Pipeline, VkPipeline>(20),
        .pipelineLayout = fakeHandle<vk::PipelineLayout, VkPipelineLayout>(21),
        .frameUniformBuffer = fakeHandle<vk::Buffer, VkBuffer>(12),
        .shadowMapView = fakeHandle<vk::ImageView, VkImageView>(30),
        .shadowComparisonSampler = fakeHandle<vk::Sampler, VkSampler>(31),
        .viewport = {.x = 0.0f,
                     .y = 600.0f,
                     .width = 800.0f,
                     .height = -600.0f,
                     .minDepth = 0.0f,
                     .maxDepth = 1.0f},
        .scissor = {.offset = {.x = 0, .y = 0}, .extent = {.width = 800, .height = 600}},
        .colorAttachmentFormat = vk::Format::eB8G8R8A8Srgb,
        .depthAttachmentFormat = vk::Format::eD32Sfloat,
        .vertexLayout = fire_engine::VertexLayoutKey::ePositionColorTextureCoordinate,
    };
}
} // namespace

using fire_engine::detail::ForwardRecordingDraw;
using fire_engine::detail::ForwardRecordingInput;
using fire_engine::detail::ForwardRecordingState;
using fire_engine::detail::FrameRecordingInput;
using fire_engine::detail::ShadowRecordingDraw;
using fire_engine::detail::ShadowRecordingInput;
using fire_engine::detail::ShadowRecordingState;

static_assert(std::is_trivially_copyable_v<ShadowRecordingState>);
static_assert(std::is_trivially_copyable_v<ShadowRecordingDraw>);
static_assert(std::is_trivially_copyable_v<ForwardRecordingState>);
static_assert(std::is_trivially_copyable_v<ForwardRecordingDraw>);
static_assert(std::is_trivially_copyable_v<fire_engine::detail::CompiledGeometry>);
static_assert(std::is_trivially_copyable_v<fire_engine::detail::CompiledForwardMaterial>);
static_assert(std::is_trivially_copyable_v<fire_engine::detail::CompiledRenderObject>);
static_assert(std::is_trivially_copyable_v<fire_engine::detail::CompiledResourcesView>);
static_assert(!std::is_default_constructible_v<fire_engine::detail::CompiledResourcesView>);

static_assert(!std::is_default_constructible_v<ShadowRecordingInput>);
static_assert(!std::is_copy_constructible_v<ShadowRecordingInput>);
static_assert(!std::is_copy_assignable_v<ShadowRecordingInput>);
static_assert(!std::is_move_constructible_v<ShadowRecordingInput>);
static_assert(!std::is_move_assignable_v<ShadowRecordingInput>);

static_assert(!std::is_default_constructible_v<ForwardRecordingInput>);
static_assert(!std::is_copy_constructible_v<ForwardRecordingInput>);
static_assert(!std::is_copy_assignable_v<ForwardRecordingInput>);
static_assert(!std::is_move_constructible_v<ForwardRecordingInput>);
static_assert(!std::is_move_assignable_v<ForwardRecordingInput>);

static_assert(!std::is_default_constructible_v<FrameRecordingInput>);
static_assert(!std::is_copy_constructible_v<FrameRecordingInput>);
static_assert(!std::is_copy_assignable_v<FrameRecordingInput>);
static_assert(!std::is_move_constructible_v<FrameRecordingInput>);
static_assert(!std::is_move_assignable_v<FrameRecordingInput>);

TEST_CASE("Frame recording input resolves both pass sequences and reuses its arenas")
{
    using fire_engine::DrawItem;
    using fire_engine::RenderObjectId;
    using fire_engine::SceneDrawList;
    using fire_engine::detail::CompiledResourceGraph;
    using fire_engine::detail::CompiledResources;
    using fire_engine::detail::FrameRecordingInputCompiler;

    CompiledResources resources;
    auto graph = std::make_unique<CompiledResourceGraph>();
    graph->objects.resize(2);
    graph->objects[0] = compiledRenderObject(1, {.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f});
    graph->objects[1] = compiledRenderObject(10, {.r = 0.0f, .g = 1.0f, .b = 0.0f, .a = 1.0f});
    resources.replace(std::move(graph));

    const std::array drawItems{
        DrawItem{.renderObject = RenderObjectId{.value = 0},
                 .world = fire_engine::Transform{.translation = {.x = 1.0f}}.matrix()},
        DrawItem{.renderObject = RenderObjectId{.value = 0},
                 .world = fire_engine::Transform{.translation = {.x = 2.0f}}.matrix()},
        DrawItem{
            .renderObject = RenderObjectId{.value = 1},
            .world =
                fire_engine::Transform{
                    .translation = {.x = 3.0f, .y = 2.0f, .z = 1.0f},
                }
                    .matrix(),
        },
    };
    const SceneDrawList drawList{.drawItems = drawItems, .dependencyHash = 0};
    FrameRecordingInputCompiler compiler;

    const ShadowRecordingDraw* firstShadowStorage = nullptr;
    const ForwardRecordingDraw* firstForwardStorage = nullptr;
    {
        const auto input = compiler.compile(drawList, resources.view(), frameUniforms(),
                                            shadowRecordingState(), forwardRecordingState());
        static_assert(
            std::is_const_v<std::remove_reference_t<decltype(input.shadow().draws().front())>>);
        static_assert(
            std::is_const_v<std::remove_reference_t<decltype(input.forward().draws().front())>>);

        REQUIRE(input.shadow().draws().size() == drawItems.size());
        REQUIRE(input.forward().draws().size() == drawItems.size());
        for (std::size_t index = 0; index < drawItems.size(); ++index)
        {
            const auto object =
                requireCompiledObject(resources.view(), drawItems[index].renderObject);
            REQUIRE(input.shadow().draws()[index].vertexBuffer == object.geometry.vertexBuffer);
            REQUIRE(input.shadow().draws()[index].indexBuffer == object.geometry.indexBuffer);
            REQUIRE(input.shadow().draws()[index].indexCount == object.geometry.indexCount);
            REQUIRE(input.shadow().draws()[index].constants.model == drawItems[index].world);
            REQUIRE(input.forward().draws()[index].vertexBuffer == object.geometry.vertexBuffer);
            REQUIRE(input.forward().draws()[index].indexBuffer == object.geometry.indexBuffer);
            REQUIRE(input.forward().draws()[index].indexCount == object.geometry.indexCount);
            REQUIRE(input.forward().draws()[index].sampler == object.forwardMaterial.sampler);
            REQUIRE(input.forward().draws()[index].imageView == object.forwardMaterial.imageView);
            REQUIRE(input.forward().state().shadowMapView != object.forwardMaterial.imageView);
            REQUIRE(input.forward().state().shadowComparisonSampler !=
                    object.forwardMaterial.sampler);
            REQUIRE(input.forward().draws()[index].constants.baseColor ==
                    object.forwardMaterial.baseColor);
            REQUIRE(input.forward().draws()[index].constants.model == drawItems[index].world);
        }
        REQUIRE(input.shadow().state().pipeline == shadowRecordingState().pipeline);
        REQUIRE(input.forward().state().pipeline == forwardRecordingState().pipeline);
        REQUIRE(input.forward().state().shadowMapView == forwardRecordingState().shadowMapView);
        REQUIRE(input.forward().state().shadowComparisonSampler ==
                forwardRecordingState().shadowComparisonSampler);
        REQUIRE(input.shadow().state().frameUniformBuffer ==
                input.forward().state().frameUniformBuffer);
        REQUIRE(input.frameUniforms().viewProjection == fire_engine::Mat4::identity());
        REQUIRE(input.frameUniforms().lightViewProjection == frameUniforms().lightViewProjection);
        REQUIRE(input.forward().state().viewport.height == -600.0f);
        firstShadowStorage = input.shadow().draws().data();
        firstForwardStorage = input.forward().draws().data();
    }
    {
        // The previous transaction has expired. Reusing packet storage must
        // still freeze the newly selected slot's view and sampling state.
        auto nextForwardState = forwardRecordingState();
        nextForwardState.shadowMapView = fakeHandle<vk::ImageView, VkImageView>(40);
        nextForwardState.shadowComparisonSampler = fakeHandle<vk::Sampler, VkSampler>(41);
        const auto rebuilt = compiler.compile(drawList, resources.view(), frameUniforms(),
                                              shadowRecordingState(), nextForwardState);
        REQUIRE(rebuilt.shadow().draws().data() == firstShadowStorage);
        REQUIRE(rebuilt.forward().draws().data() == firstForwardStorage);
        REQUIRE(rebuilt.forward().state().shadowMapView == nextForwardState.shadowMapView);
        REQUIRE(rebuilt.forward().state().shadowComparisonSampler ==
                nextForwardState.shadowComparisonSampler);
    }
}

TEST_CASE("Frame recording input filters only the ordered shadow sequence")
{
    using fire_engine::DrawItem;
    using fire_engine::RenderObjectId;
    using fire_engine::SceneDrawList;
    using fire_engine::detail::CompiledResourceGraph;
    using fire_engine::detail::CompiledResources;
    using fire_engine::detail::FrameRecordingInputCompiler;

    constexpr RenderObjectId cubeCaster{.value = 0};
    constexpr RenderObjectId receiveOnlyPlane{.value = 1};
    constexpr RenderObjectId secondCaster{.value = 2};

    CompiledResources resources;
    auto graph = std::make_unique<CompiledResourceGraph>();
    graph->objects.resize(3);
    graph->objects[cubeCaster.value] =
        compiledRenderObject(1, {.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f});
    graph->objects[receiveOnlyPlane.value] =
        compiledRenderObject(10, {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f}, false);
    graph->objects[secondCaster.value] =
        compiledRenderObject(20, {.r = 0.0f, .g = 0.0f, .b = 1.0f, .a = 1.0f});
    resources.replace(std::move(graph));

    const std::array drawItems{
        DrawItem{.renderObject = cubeCaster},
        DrawItem{.renderObject = receiveOnlyPlane,
                 .world = fire_engine::Transform{.translation = {.y = -1.0f}}.matrix()},
        DrawItem{.renderObject = secondCaster,
                 .world = fire_engine::Transform{.translation = {.x = 2.0f}}.matrix()},
    };
    FrameRecordingInputCompiler compiler;
    const auto input =
        compiler.compile(SceneDrawList{.drawItems = drawItems}, resources.view(), frameUniforms(),
                         shadowRecordingState(), forwardRecordingState());

    const auto cube = requireCompiledObject(resources.view(), cubeCaster);
    const auto receiver = requireCompiledObject(resources.view(), receiveOnlyPlane);
    const auto second = requireCompiledObject(resources.view(), secondCaster);
    REQUIRE(cube.castsShadow);
    REQUIRE_FALSE(receiver.castsShadow);
    REQUIRE(second.castsShadow);

    const auto shadow = input.shadow().draws();
    REQUIRE(shadow.size() == 2);
    REQUIRE(shadow[0].vertexBuffer == cube.geometry.vertexBuffer);
    REQUIRE(shadow[0].indexBuffer == cube.geometry.indexBuffer);
    REQUIRE(shadow[0].indexCount == cube.geometry.indexCount);
    REQUIRE(shadow[0].constants.model == drawItems[0].world);
    REQUIRE(shadow[1].vertexBuffer == second.geometry.vertexBuffer);
    REQUIRE(shadow[1].indexBuffer == second.geometry.indexBuffer);
    REQUIRE(shadow[1].indexCount == second.geometry.indexCount);
    REQUIRE(shadow[1].constants.model == drawItems[2].world);

    const auto forward = input.forward().draws();
    REQUIRE(forward.size() == drawItems.size());
    REQUIRE(forward[0].vertexBuffer == cube.geometry.vertexBuffer);
    REQUIRE(forward[0].sampler == cube.forwardMaterial.sampler);
    REQUIRE(forward[0].constants.model == drawItems[0].world);
    REQUIRE(forward[1].vertexBuffer == receiver.geometry.vertexBuffer);
    REQUIRE(forward[1].indexBuffer == receiver.geometry.indexBuffer);
    REQUIRE(forward[1].indexCount == receiver.geometry.indexCount);
    REQUIRE(forward[1].sampler == receiver.forwardMaterial.sampler);
    REQUIRE(forward[1].imageView == receiver.forwardMaterial.imageView);
    REQUIRE(forward[1].constants.baseColor == receiver.forwardMaterial.baseColor);
    REQUIRE(forward[1].constants.model == drawItems[1].world);
    REQUIRE(forward[2].vertexBuffer == second.geometry.vertexBuffer);
    REQUIRE(forward[2].sampler == second.forwardMaterial.sampler);
    REQUIRE(forward[2].constants.model == drawItems[2].world);
}

TEST_CASE("Frame recording input rejects mismatched, unresolved, and incompatible draws")
{
    using fire_engine::DrawItem;
    using fire_engine::RenderObjectId;
    using fire_engine::SceneDrawList;
    using fire_engine::detail::CompiledResourceGraph;
    using fire_engine::detail::CompiledResources;
    using fire_engine::detail::FrameRecordingInputCompiler;

    CompiledResources resources;
    auto graph = std::make_unique<CompiledResourceGraph>();
    graph->objects.resize(2);
    graph->objects[0] = compiledRenderObject(1, {});
    auto incompatible = compiledRenderObject(10, {});
    incompatible.geometry.vertexLayout = static_cast<fire_engine::VertexLayoutKey>(255);
    graph->objects[1] = incompatible;
    resources.replace(std::move(graph));

    FrameRecordingInputCompiler compiler;
    const std::array missingItems{DrawItem{.renderObject = RenderObjectId{.value = 2}}};

    auto mismatchedShadowState = shadowRecordingState();
    mismatchedShadowState.vertexLayout = static_cast<fire_engine::VertexLayoutKey>(255);
    REQUIRE_THROWS_WITH(compiler.compile(SceneDrawList{.drawItems = missingItems}, resources.view(),
                                         frameUniforms(), mismatchedShadowState,
                                         forwardRecordingState()),
                        "Shadow and forward recording pipelines use different vertex layouts");

    REQUIRE_THROWS_WITH(compiler.compile(SceneDrawList{.drawItems = missingItems}, resources.view(),
                                         frameUniforms(), shadowRecordingState(),
                                         forwardRecordingState()),
                        "Scene refers to an object not compiled by prepare");

    const std::array incompatibleItems{DrawItem{.renderObject = RenderObjectId{.value = 1}}};
    REQUIRE_THROWS_WITH(compiler.compile(SceneDrawList{.drawItems = incompatibleItems},
                                         resources.view(), frameUniforms(), shadowRecordingState(),
                                         forwardRecordingState()),
                        "Compiled geometry is incompatible with the frame recording pipelines");
}
