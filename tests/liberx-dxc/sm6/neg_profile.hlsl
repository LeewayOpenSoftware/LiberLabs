// LiberX DXC L2 negative fixture: valid source compiled with a profile that does not exist (cs_7_0).
RWStructuredBuffer<float> data : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    data[id.x] = data[id.x] + 1.0f;
}
