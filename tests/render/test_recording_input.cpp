#include "fire_engine/render/detail/recording_input.hpp"

#include <fire_engine/math/transform.hpp>
#include <fire_engine/render/detail/compiled_resource_graph.hpp>
#include <fire_engine/scene/scene_draw_list.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
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
        return Handle{reinterpret_cast<NativeHandle>(value)};
    }
    else
    {
        return Handle{static_cast<NativeHandle>(value)};
    }
}

[[nodiscard]] fire_engine::detail::CompiledDraw compiledDraw(std::uintptr_t firstHandle,
                                                             fire_engine::Color4 baseColor)
{
    return {
        .vertexBuffer = fakeHandle<vk::Buffer, VkBuffer>(firstHandle),
        .indexBuffer = fakeHandle<vk::Buffer, VkBuffer>(firstHandle + 1),
        .indexCount = static_cast<std::uint32_t>(firstHandle),
        .sampler = fakeHandle<vk::Sampler, VkSampler>(firstHandle + 2),
        .imageView = fakeHandle<vk::ImageView, VkImageView>(firstHandle + 3),
        .baseColor = baseColor,
        .vertexLayout = fire_engine::VertexLayoutKey::ePositionColorTextureCoordinate,
    };
}

[[nodiscard]] fire_engine::detail::RecordingState recordingState()
{
    return {
        .pipeline = fakeHandle<vk::Pipeline, VkPipeline>(20),
        .pipelineLayout = fakeHandle<vk::PipelineLayout, VkPipelineLayout>(21),
        .frameUniformBuffer = fakeHandle<vk::Buffer, VkBuffer>(22),
        .frameUniforms = {.viewProjection = fire_engine::Mat4::identity()},
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

static_assert(std::is_trivially_copyable_v<fire_engine::detail::RecordingState>);
static_assert(std::is_trivially_copyable_v<fire_engine::detail::RecordingDraw>);
static_assert(std::is_trivially_copyable_v<fire_engine::detail::CompiledDraw>);
static_assert(std::is_trivially_copyable_v<fire_engine::detail::CompiledResourcesView>);
static_assert(!std::is_default_constructible_v<fire_engine::detail::CompiledResourcesView>);
static_assert(!std::is_default_constructible_v<fire_engine::detail::RecordingInput>);
static_assert(!std::is_copy_constructible_v<fire_engine::detail::RecordingInput>);
static_assert(!std::is_copy_assignable_v<fire_engine::detail::RecordingInput>);
static_assert(!std::is_move_constructible_v<fire_engine::detail::RecordingInput>);
static_assert(!std::is_move_assignable_v<fire_engine::detail::RecordingInput>);

TEST_CASE("Recording input resolves immutable packets and reuses its arena")
{
    using fire_engine::DrawItem;
    using fire_engine::RenderObjectId;
    using fire_engine::SceneDrawList;
    using fire_engine::detail::CompiledResourceGraph;
    using fire_engine::detail::CompiledResources;
    using fire_engine::detail::RecordingDraw;
    using fire_engine::detail::RecordingInputCompiler;

    CompiledResources resources;
    auto graph = std::make_unique<CompiledResourceGraph>();
    graph->objects.resize(2);
    graph->objects[0] = compiledDraw(1, {.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f});
    graph->objects[1] = compiledDraw(10, {.r = 0.0f, .g = 1.0f, .b = 0.0f, .a = 1.0f});
    resources.replace(std::move(graph));

    const std::array drawItems{
        DrawItem{.renderObject = RenderObjectId{.value = 0}},
        DrawItem{.renderObject = RenderObjectId{.value = 0}},
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
    RecordingInputCompiler compiler;

    const RecordingDraw* firstStorage = nullptr;
    {
        const auto input = compiler.compile(drawList, resources.view(), recordingState());
        static_assert(std::is_const_v<std::remove_reference_t<decltype(input.draws().front())>>);
        REQUIRE(input.draws().size() == 3);
        REQUIRE(input.draws()[0].vertexBuffer == input.draws()[1].vertexBuffer);
        REQUIRE(input.draws()[0].constants.baseColor.r == 1.0f);
        REQUIRE(input.draws()[2].vertexBuffer != input.draws()[1].vertexBuffer);
        REQUIRE(input.draws()[2].constants.baseColor.g == 1.0f);
        REQUIRE(input.draws()[2].constants.model == drawItems[2].world);
        REQUIRE(input.state().pipeline == recordingState().pipeline);
        REQUIRE(input.state().frameUniforms.viewProjection == fire_engine::Mat4::identity());
        REQUIRE(input.state().viewport.height == -600.0f);
        firstStorage = input.draws().data();
    }
    {
        const auto rebuilt = compiler.compile(drawList, resources.view(), recordingState());
        REQUIRE(rebuilt.draws().data() == firstStorage);
    }
}

TEST_CASE("Recording input rejects unresolved and pipeline-incompatible draws")
{
    using fire_engine::DrawItem;
    using fire_engine::RenderObjectId;
    using fire_engine::SceneDrawList;
    using fire_engine::detail::CompiledResourceGraph;
    using fire_engine::detail::CompiledResources;
    using fire_engine::detail::RecordingInputCompiler;

    CompiledResources resources;
    auto graph = std::make_unique<CompiledResourceGraph>();
    graph->objects.resize(2);
    graph->objects[0] = compiledDraw(1, {});
    auto incompatible = compiledDraw(10, {});
    incompatible.vertexLayout = static_cast<fire_engine::VertexLayoutKey>(255);
    graph->objects[1] = incompatible;
    resources.replace(std::move(graph));

    RecordingInputCompiler compiler;
    const std::array missingItems{
        DrawItem{.renderObject = RenderObjectId{.value = 2}},
    };
    REQUIRE_THROWS_AS(compiler.compile(SceneDrawList{.drawItems = missingItems}, resources.view(),
                                       recordingState()),
                      std::logic_error);

    const std::array incompatibleItems{
        DrawItem{.renderObject = RenderObjectId{.value = 1}},
    };
    REQUIRE_THROWS_AS(compiler.compile(SceneDrawList{.drawItems = incompatibleItems},
                                       resources.view(), recordingState()),
                      std::logic_error);
}
