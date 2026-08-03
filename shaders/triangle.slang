struct FrameUniforms
{
    float4x4 transform;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms, Std140DataLayout> frame;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;
};

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
};

[shader("vertex")]
FragmentInput vertexMain(VertexInput input)
{
    FragmentInput output;
    output.position = mul(frame.transform, float4(input.position, 0.0, 1.0));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target
{
    return float4(input.color, 1.0);
}
