struct FrameUniforms
{
    float4x4 viewProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms, Std140DataLayout> frame;

struct DrawConstants
{
    float4x4 model;
    float4 baseColour;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> draw;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 colour : COLOR;
};

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 colour : COLOR;
};

[shader("vertex")]
FragmentInput vertexMain(VertexInput input)
{
    FragmentInput output;
    output.position = mul(frame.viewProjection, mul(draw.model, float4(input.position, 1.0)));
    output.colour = input.colour * draw.baseColour;
    return output;
}

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target
{
    return input.colour;
}
