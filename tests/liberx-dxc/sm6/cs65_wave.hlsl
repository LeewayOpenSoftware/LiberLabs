// LiberX DXC L2 fixture: Shader Model 6.5 wave intrinsics (WaveMatch, WaveMultiPrefixSum).
RWStructuredBuffer<uint> out0 : register(u0);
StructuredBuffer<uint>   keys : register(t0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint key = keys[id.x];
    uint4 mask = WaveMatch(key);
    uint prefix = WaveMultiPrefixSum(1u, mask);
    out0[id.x] = prefix + countbits(mask.x);
}
