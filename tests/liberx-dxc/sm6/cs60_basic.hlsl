// LiberX DXC L2 fixture: Shader Model 6.0 compute, structured buffer read/write.
RWStructuredBuffer<float> data : register(u0);

cbuffer Params : register(b0)
{
    float scale;
    uint count;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x < count)
        data[id.x] = data[id.x] * scale + 1.0f;
}
