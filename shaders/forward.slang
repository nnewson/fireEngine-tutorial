#include "frame_uniforms.slang"

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms, Std140DataLayout> frame;

[[vk::binding(1, 0)]]
Sampler2D<float4> baseColorTexture;

[[vk::binding(2, 0)]]
Sampler2DShadow shadowMap;

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
    // Light-clip coordinates are affine in world position. Camera-perspective-
    // correct interpolation reconstructs them at the fragment's world position;
    // noperspective would introduce a view-dependent error that resembles bad bias.
    [[vk::location(2)]] float4 lightClipPosition : TEXCOORD1;
};

[shader("vertex")]
FragmentInput vertexMain(VertexInput input)
{
    FragmentInput output;
    const float4 worldPosition = mul(draw.model, float4(input.position, 1.0));
    output.position = mul(frame.viewProjection, worldPosition);
    output.color = input.color * draw.baseColor;
    output.textureCoordinate = input.textureCoordinate;
    output.lightClipPosition = mul(frame.lightViewProjection, worldPosition);
    return output;
}

float directionalShadowVisibility(float4 lightClipPosition)
{
    if (lightClipPosition.w <= 0.0)
    {
        return 1.0;
    }
    const float3 projected = lightClipPosition.xyz / lightClipPosition.w;
    // Reject receiver depth before applying bias. XY outside the map deliberately
    // reaches the sampler's opaque-white border instead of clamping an edge texel.
    if (projected.z < 0.0 || projected.z > 1.0)
    {
        return 1.0;
    }
    const float2 uv = float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
    return shadowMap.SampleCmpLevelZero(uv, projected.z - 0.0015);
}

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target
{
    // Keep the derivative-dependent base-color sample outside the shadow helper's
    // depth-dependent branches. Shadow comparison itself uses explicit mip zero.
    const float4 base = baseColorTexture.Sample(input.textureCoordinate) * input.color;
    const float visibility = directionalShadowVisibility(input.lightClipPosition);
    return float4(base.rgb * lerp(0.35, 1.0, visibility), base.a);
}
