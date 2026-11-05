// LiberX DXC L2 negative fixture: int16_t without -enable-16bit-types is an unknown type.
RWStructuredBuffer<int16_t> data : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    data[id.x] = data[id.x] + (int16_t)1;
}
