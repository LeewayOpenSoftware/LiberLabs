// LiberX DXC L2 negative fixture: syntax error (missing semicolon) must fail with a diagnostic.
RWStructuredBuffer<float> data : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float v = data[id.x]
    data[id.x] = v + 1.0f;
}
