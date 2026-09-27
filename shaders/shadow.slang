#include "frame_uniforms.slang"

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms, Std140DataLayout> frame;

struct ShadowDrawConstants
{
    float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<ShadowDrawConstants> draw;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

[shader("vertex")]
float4 vertexMain(VertexInput input) : SV_Position
{
    return mul(frame.lightViewProjection, mul(draw.model, float4(input.position, 1.0)));
}
