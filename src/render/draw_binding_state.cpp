#include <fire_engine/render/detail/draw_binding_state.hpp>

namespace fire_engine::detail
{
/* --- Public member functions --- */

DrawBindingChanges DrawBindingState::update(vk::Buffer vertexBuffer, vk::Buffer indexBuffer,
                                            vk::Sampler sampler, vk::ImageView imageView) noexcept
{
    const bool geometryChanged =
        !hasPreviousDraw_ || vertexBuffer_ != vertexBuffer || indexBuffer_ != indexBuffer;
    const bool textureChanged = !hasPreviousDraw_ || sampler_ != sampler || imageView_ != imageView;

    vertexBuffer_ = vertexBuffer;
    indexBuffer_ = indexBuffer;
    sampler_ = sampler;
    imageView_ = imageView;
    hasPreviousDraw_ = true;

    return {
        .geometry = geometryChanged,
        .texture = textureChanged,
    };
}
} // namespace fire_engine::detail
