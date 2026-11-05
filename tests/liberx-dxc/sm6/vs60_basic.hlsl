// LiberX DXC L2 fixture: Shader Model 6.0 vertex shader with a constant buffer.
cbuffer Transform : register(b0)
{
    float4x4 mvp;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut main(float3 pos : POSITION, float2 uv : TEXCOORD0)
{
    VSOut o;
    o.pos = mul(mvp, float4(pos, 1.0f));
    o.uv = uv;
    return o;
}
