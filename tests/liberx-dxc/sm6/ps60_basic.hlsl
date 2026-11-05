// LiberX DXC L2 fixture: Shader Model 6.0 pixel shader, texture sample.
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

cbuffer Tint : register(b0)
{
    float4 tint;
};

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    return tex.Sample(samp, uv) * tint;
}
