struct FrameUniforms
{
    float4x4 viewProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms, Std140DataLayout> frame;

[[vk::binding(1, 0)]]
Sampler2D<float4> baseColorTexture;

struct DrawConstants
{
    float4x4 model;
    float4 baseColor;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> draw;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float2 textureCoordinate : TEXCOORD;
};

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
    [[vk::location(1)]] float2 textureCoordinate : TEXCOORD;
};

[shader("vertex")]
FragmentInput vertexMain(VertexInput input)
{
    FragmentInput output;
    output.position = mul(frame.viewProjection, mul(draw.model, float4(input.position, 1.0)));
    output.color = input.color * draw.baseColor;
    output.textureCoordinate = input.textureCoordinate;
    return output;
}

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target
{
    return baseColorTexture.Sample(input.textureCoordinate) * input.color;
}
