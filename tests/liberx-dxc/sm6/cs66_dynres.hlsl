// LiberX DXC L2 fixture: Shader Model 6.6 dynamic resources and 64-bit atomics.
RWStructuredBuffer<uint64_t> counters : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    RWBuffer<uint> dyn = ResourceDescriptorHeap[0];
    uint v = dyn[id.x];
    uint64_t prev;
    InterlockedAdd(counters[0], (uint64_t)v, prev);
    dyn[id.x] = (uint)prev;
}
