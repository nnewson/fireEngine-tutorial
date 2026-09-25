#include <fire_engine/render/detail/frame_uniform_buffer.hpp>

#include <fire_engine/render/detail/allocator.hpp>

#include <span>

namespace fire_engine::detail
{
/** @cond INTERNAL */
/* --- Internal member functions --- */

FrameUniformBuffer::FrameUniformBuffer(const MemoryAllocator& allocator)
    : buffer_{allocator, sizeof(FrameUniforms), vk::BufferUsageFlagBits::eUniformBuffer}
{
    constexpr FrameUniforms initialUniforms{};
    buffer_.write(std::as_bytes(std::span{&initialUniforms, 1}));
}

vk::Buffer FrameUniformBuffer::handle() const noexcept
{
    return buffer_.handle();
}

void FrameUniformBuffer::update(const FrameUniforms& uniforms) const
{
    buffer_.write(std::as_bytes(std::span{&uniforms, 1}));
}
/** @endcond */
} // namespace fire_engine::detail
