#include "fire_engine/render/detail/draw_binding_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
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
} // namespace

static_assert(!std::is_copy_constructible_v<fire_engine::detail::DrawBindingState>);
static_assert(!std::is_copy_assignable_v<fire_engine::detail::DrawBindingState>);
static_assert(std::is_nothrow_move_constructible_v<fire_engine::detail::DrawBindingState>);
static_assert(std::is_nothrow_move_assignable_v<fire_engine::detail::DrawBindingState>);

TEST_CASE("Draw binding state emits only changed geometry and textures")
{
    using fire_engine::detail::DrawBindingChanges;
    using fire_engine::detail::DrawBindingState;

    const vk::Buffer firstVertex = fakeHandle<vk::Buffer, VkBuffer>(1);
    const vk::Buffer firstIndex = fakeHandle<vk::Buffer, VkBuffer>(2);
    const vk::Buffer secondVertex = fakeHandle<vk::Buffer, VkBuffer>(3);
    const vk::Buffer secondIndex = fakeHandle<vk::Buffer, VkBuffer>(4);
    const vk::Sampler firstSampler = fakeHandle<vk::Sampler, VkSampler>(5);
    const vk::ImageView firstView = fakeHandle<vk::ImageView, VkImageView>(6);
    const vk::Sampler secondSampler = fakeHandle<vk::Sampler, VkSampler>(7);
    const vk::ImageView secondView = fakeHandle<vk::ImageView, VkImageView>(8);

    DrawBindingState state;
    REQUIRE((state.update(firstVertex, firstIndex, firstSampler, firstView) ==
             DrawBindingChanges{.geometry = true, .texture = true}));
    REQUIRE((state.update(firstVertex, firstIndex, firstSampler, firstView) ==
             DrawBindingChanges{.geometry = false, .texture = false}));
    REQUIRE((state.update(firstVertex, firstIndex, secondSampler, firstView) ==
             DrawBindingChanges{.geometry = false, .texture = true}));
    REQUIRE((state.update(firstVertex, firstIndex, secondSampler, secondView) ==
             DrawBindingChanges{.geometry = false, .texture = true}));
    REQUIRE((state.update(secondVertex, firstIndex, secondSampler, secondView) ==
             DrawBindingChanges{.geometry = true, .texture = false}));
    REQUIRE((state.update(secondVertex, secondIndex, secondSampler, secondView) ==
             DrawBindingChanges{.geometry = true, .texture = false}));
    REQUIRE((state.update(firstVertex, firstIndex, firstSampler, firstView) ==
             DrawBindingChanges{.geometry = true, .texture = true}));
}
