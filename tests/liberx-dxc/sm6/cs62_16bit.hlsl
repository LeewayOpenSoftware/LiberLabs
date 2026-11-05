// LiberX DXC L2 fixture: Shader Model 6.2 native 16-bit types (needs -enable-16bit-types).
RWStructuredBuffer<half>     out16 : register(u0);
StructuredBuffer<int16_t>    in16  : register(t0);
StructuredBuffer<uint16_t>   inu16 : register(t1);

[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int16_t  a = in16[id.x];
    uint16_t b = inu16[id.x];
    half     h = (half)a * (half)0.5 + (half)b;
    out16[id.x] = h;
}
