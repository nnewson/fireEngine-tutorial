#include <fire_engine/render/detail/forward_frame_uniform_buffer.hpp>

#include <fire_engine/render/detail/allocator.hpp>

#include <span>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

ForwardFrameUniformBuffer::ForwardFrameUniformBuffer(const MemoryAllocator& allocator)
    : buffer_{allocator, sizeof(FrameUniforms), vk::BufferUsageFlagBits::eUniformBuffer}
{
    constexpr FrameUniforms initialUniforms{.viewProjection = Mat4::identity()};
    buffer_.write(std::as_bytes(std::span{&initialUniforms, 1}));
}

vk::Buffer ForwardFrameUniformBuffer::handle() const noexcept
{
    return buffer_.handle();
}

void ForwardFrameUniformBuffer::update(const FrameUniforms& uniforms) const
{
    buffer_.write(std::as_bytes(std::span{&uniforms, 1}));
}
/** @endcond */
} // namespace fire_engine::detail
