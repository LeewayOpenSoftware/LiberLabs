// LiberX DXC L2 fixture: Shader Model 6.0 wave intrinsics (reduction, lane index, first lane).
RWStructuredBuffer<uint> sums : register(u0);
StructuredBuffer<uint>   src  : register(t0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    uint v = src[id.x];
    uint total = WaveActiveSum(v);
    uint lane = WaveGetLaneIndex();
    if (WaveIsFirstLane())
        sums[gi / WaveGetLaneCount()] = total + lane;
}
