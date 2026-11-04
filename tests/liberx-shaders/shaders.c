#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>

#define SHADER_SUITE_VERSION 1
#define LIBERX_SHVER_PIXEL 0
#define LIBERX_SHVER_VERTEX 1
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static const GUID iid_reflection47 = {0x8d536ca1, 0x0cca, 0x4956, {0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84}};

typedef HRESULT (WINAPI *compile_func)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
        ID3DInclude *, const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);
typedef HRESULT (WINAPI *reflect_func)(const void *, SIZE_T, REFIID, void **);

static compile_func p_compile;
static reflect_func p_reflect;
static int pass_count, fail_count, skip_count;

static void report(const char *group, const char *name, const char *status, const char *fmt, ...)
{
    char detail[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    printf("SHADER v%d %s %s %s %s\n", SHADER_SUITE_VERSION, group, name, status, detail);
    if (status[0] == 'F') ++fail_count;
    else if (status[0] == 'S') ++skip_count;
    else ++pass_count;
}

static void check(const char *group, const char *name, BOOL ok, const char *fmt, ...)
{
    char detail[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    report(group, name, ok ? "PASS" : "FAIL", "%s", detail);
}

static const char vs_source[] =
    "cbuffer Params : register(b0)\n"
    "{\n"
    "    float4x4 world;\n"
    "    float4 tint;\n"
    "    float scale;\n"
    "};\n"
    "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };\n"
    "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
    "VSOut main(VSIn i)\n"
    "{\n"
    "    VSOut o;\n"
    "    o.pos = mul(world, float4(i.pos * scale, 1.0));\n"
    "    o.uv = i.uv;\n"
    "    o.col = tint;\n"
    "    return o;\n"
    "}\n";

static const char ps_source[] =
    "Texture2D tex : register(t0);\n"
    "SamplerState samp : register(s0);\n"
    "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    return tex.Sample(samp, uv);\n"
    "}\n";

static const char cs_source[] =
    "RWStructuredBuffer<uint> output : register(u0);\n"
    "[numthreads(8, 4, 2)]\n"
    "void main(uint3 id : SV_DispatchThreadID)\n"
    "{\n"
    "    output[id.x] = id.x;\n"
    "}\n";

static ID3DBlob *compile(const char *group, const char *name, const char *source, const char *profile, UINT flags,
        BOOL expect_success)
{
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr;

    hr = p_compile(source, strlen(source), "liberx.hlsl", NULL, NULL, "main", profile, flags, 0, &code, &errors);
    if (expect_success)
        check(group, name, SUCCEEDED(hr) && code, "profile %s hr=%#lx", profile, hr);
    else
        check(group, name, FAILED(hr) && !code && errors, "profile %s hr=%#lx errors=%p", profile, hr, errors);
    if (errors) ID3D10Blob_Release(errors);
    if (FAILED(hr) && code)
    {
        ID3D10Blob_Release(code);
        code = NULL;
    }
    return code;
}

static void validate_container(const char *group, const char *name, ID3DBlob *blob, const char *required[], unsigned int required_count)
{
    const BYTE *data = ID3D10Blob_GetBufferPointer(blob);
    SIZE_T size = ID3D10Blob_GetBufferSize(blob);
    const DWORD *header = (const DWORD *)data;
    DWORD chunk_count, i, j;
    BOOL bounds = TRUE;
    BOOL found[8] = {0};

    if (size < 32 || memcmp(data, "DXBC", 4))
    {
        report(group, name, "FAIL", "no DXBC magic or size %Iu", size);
        return;
    }
    check(group, name, header[6] == size, "container size field %lu blob size %Iu", header[6], size);
    chunk_count = header[7];
    if (chunk_count > 64 || 32 + chunk_count * 4 > size)
    {
        report(group, name, "FAIL", "chunk count %lu out of range", chunk_count);
        return;
    }
    for (i = 0; i < chunk_count; ++i)
    {
        DWORD offset = header[8 + i];
        DWORD chunk_size;

        if (offset + 8 > size) { bounds = FALSE; break; }
        chunk_size = *(const DWORD *)(data + offset + 4);
        if (offset + 8 + chunk_size > size) { bounds = FALSE; break; }
        for (j = 0; j < required_count && j < COUNT_OF(found); ++j)
            if (!memcmp(data + offset, required[j], 4)) found[j] = TRUE;
    }
    check(group, name, bounds, "%lu chunks inside the container", chunk_count);
    for (j = 0; j < required_count && j < COUNT_OF(found); ++j)
        check(group, name, found[j], "chunk %s present", required[j]);
}

static ID3D11ShaderReflection *reflect(const char *group, const char *name, ID3DBlob *code)
{
    ID3D11ShaderReflection *r = NULL;
    HRESULT hr = p_reflect(ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), &iid_reflection47,
            (void **)&r);

    check(group, name, SUCCEEDED(hr) && r, "D3DReflect hr=%#lx", hr);
    return r;
}

static void test_vertex_shader(void)
{
    static const char *chunks[] = {"RDEF", "ISGN", "OSGN"};
    ID3D11ShaderReflectionConstantBuffer *cb;
    D3D11_SHADER_BUFFER_DESC cb_desc;
    D3D11_SHADER_DESC desc;
    ID3D11ShaderReflection *r;
    ID3DBlob *code;
    HRESULT hr;

    code = compile("vs_5_0", "compile", vs_source, "vs_5_0", 0, TRUE);
    if (!code) return;
    validate_container("vs_5_0", "container", code, chunks, 3);
    if (!(r = reflect("vs_5_0", "reflect", code))) { ID3D10Blob_Release(code); return; }

    memset(&desc, 0, sizeof(desc));
    hr = r->lpVtbl->GetDesc(r, &desc);
    check("vs_5_0", "desc", hr == S_OK && desc.InstructionCount > 0, "hr=%#lx instructions=%u", hr,
            desc.InstructionCount);
    check("vs_5_0", "desc-version", ((desc.Version >> 16) & 0xffff) == LIBERX_SHVER_VERTEX,
            "version %#x", desc.Version);
    check("vs_5_0", "input-count", desc.InputParameters == 2, "%u", desc.InputParameters);
    check("vs_5_0", "output-count", desc.OutputParameters == 3, "%u", desc.OutputParameters);
    check("vs_5_0", "cbuffer-count", desc.ConstantBuffers == 1, "%u", desc.ConstantBuffers);
    check("vs_5_0", "bound-count", desc.BoundResources == 1, "%u", desc.BoundResources);

    cb = r->lpVtbl->GetConstantBufferByIndex(r, 0);
    memset(&cb_desc, 0, sizeof(cb_desc));
    hr = cb->lpVtbl->GetDesc(cb, &cb_desc);
    check("vs_5_0", "cbuffer-desc", hr == S_OK && cb_desc.Name && !strcmp(cb_desc.Name, "Params")
            && cb_desc.Variables == 3, "hr=%#lx name=%s variables=%u", hr, cb_desc.Name ? cb_desc.Name : "(null)",
            cb_desc.Variables);
    check("vs_5_0", "cbuffer-size", cb_desc.Size == 96, "size %u (84 bytes of data, 16-byte rows)", cb_desc.Size);

    if (hr == S_OK && cb_desc.Variables == 3)
    {
        static const struct { const char *name; UINT offset; UINT size; } expected[] =
        {
            {"world", 0, 64}, {"tint", 64, 16}, {"scale", 80, 4},
        };
        unsigned int i;

        for (i = 0; i < COUNT_OF(expected); ++i)
        {
            ID3D11ShaderReflectionVariable *v = cb->lpVtbl->GetVariableByIndex(cb, i);
            D3D11_SHADER_VARIABLE_DESC vd;

            memset(&vd, 0, sizeof(vd));
            hr = v->lpVtbl->GetDesc(v, &vd);
            check("vs_5_0", expected[i].name, hr == S_OK && vd.Name && !strcmp(vd.Name, expected[i].name)
                    && vd.StartOffset == expected[i].offset && vd.Size == expected[i].size,
                    "hr=%#lx name=%s offset=%u size=%u", hr, vd.Name ? vd.Name : "(null)", vd.StartOffset, vd.Size);
        }
    }

    r->lpVtbl->Release(r);
    ID3D10Blob_Release(code);
}

static void test_pixel_shader(void)
{
    static const char *chunks[] = {"RDEF", "ISGN", "OSGN"};
    D3D11_SHADER_DESC desc;
    ID3D11ShaderReflection *r;
    ID3DBlob *code;
    HRESULT hr;

    code = compile("ps_5_0", "compile", ps_source, "ps_5_0", 0, TRUE);
    if (!code) return;
    validate_container("ps_5_0", "container", code, chunks, 3);
    if (!(r = reflect("ps_5_0", "reflect", code))) { ID3D10Blob_Release(code); return; }

    memset(&desc, 0, sizeof(desc));
    hr = r->lpVtbl->GetDesc(r, &desc);
    check("ps_5_0", "desc-version", hr == S_OK && ((desc.Version >> 16) & 0xffff) == LIBERX_SHVER_PIXEL,
            "hr=%#lx version %#x", hr, desc.Version);
    check("ps_5_0", "bound-count", desc.BoundResources == 2, "%u (texture and sampler)", desc.BoundResources);
    check("ps_5_0", "texture-sample", desc.TextureNormalInstructions == 1, "%u", desc.TextureNormalInstructions);
    check("ps_5_0", "movc-count-consistent", r->lpVtbl->GetMovcInstructionCount(r) <= desc.InstructionCount,
            "%u of %u", r->lpVtbl->GetMovcInstructionCount(r), desc.InstructionCount);
    check("ps_5_0", "bitwise-count-consistent",
            r->lpVtbl->GetBitwiseInstructionCount(r) <= desc.InstructionCount, "%u of %u",
            r->lpVtbl->GetBitwiseInstructionCount(r), desc.InstructionCount);
    r->lpVtbl->Release(r);
    ID3D10Blob_Release(code);
}

static void test_compute_shader(void)
{
    UINT x = 0, y = 0, z = 0, total, sy = 0, sz = 0;
    ID3D11ShaderReflection *r;
    ID3DBlob *code;

    code = compile("cs_5_0", "compile", cs_source, "cs_5_0", 0, TRUE);
    if (!code) return;
    if (!(r = reflect("cs_5_0", "reflect", code))) { ID3D10Blob_Release(code); return; }

    total = r->lpVtbl->GetThreadGroupSize(r, &x, &y, &z);
    check("cs_5_0", "thread-group", x == 8 && y == 4 && z == 2 && total == 64, "%ux%ux%u total %u", x, y, z, total);
    total = r->lpVtbl->GetThreadGroupSize(r, NULL, &sy, &sz);
    check("cs_5_0", "thread-group-null-arg", total == 0, "total %#x for a NULL argument", total);
    r->lpVtbl->Release(r);
    ID3D10Blob_Release(code);
}

static void test_profile_matrix(void)
{
    static const struct { const char *profile; const char *source; BOOL required; } matrix[] =
    {
        {"vs_4_0", vs_source, TRUE}, {"vs_4_1", vs_source, TRUE}, {"vs_5_0", vs_source, TRUE},
        {"ps_4_0", ps_source, TRUE}, {"ps_4_1", ps_source, TRUE}, {"ps_5_0", ps_source, TRUE},
        {"cs_5_0", cs_source, TRUE},
        {"vs_3_0", vs_source, FALSE}, {"ps_3_0", ps_source, FALSE},
    };
    static const UINT optimization[] = {0, D3DCOMPILE_SKIP_OPTIMIZATION, D3DCOMPILE_OPTIMIZATION_LEVEL3};
    unsigned int i, j;

    for (i = 0; i < COUNT_OF(matrix); ++i)
    {
        for (j = 0; j < COUNT_OF(optimization); ++j)
        {
            ID3DBlob *code = NULL, *errors = NULL;
            char name[64];
            HRESULT hr;

            snprintf(name, sizeof(name), "%s-opt%u", matrix[i].profile, j);
            hr = p_compile(matrix[i].source, strlen(matrix[i].source), "liberx.hlsl", NULL, NULL, "main",
                    matrix[i].profile, optimization[j], 0, &code, &errors);
            if (matrix[i].required)
                check("matrix", name, SUCCEEDED(hr) && code && ID3D10Blob_GetBufferSize(code) > 32, "hr=%#lx", hr);
            else
                report("matrix", name, SUCCEEDED(hr) ? "PASS" : "SKIP", "legacy profile recorded, hr=%#lx", hr);
            if (code) ID3D10Blob_Release(code);
            if (errors) ID3D10Blob_Release(errors);
        }
    }
}

int main(void)
{
    HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
    ID3DBlob *code;

    printf("SHADER-SUITE v%d begin\n", SHADER_SUITE_VERSION);
    if (!module || !(p_compile = (compile_func)GetProcAddress(module, "D3DCompile"))
            || !(p_reflect = (reflect_func)GetProcAddress(module, "D3DReflect")))
    {
        report("load", "d3dcompiler_47", "FAIL", "module or exports missing, error %lu", GetLastError());
        printf("SHADER-SUITE v%d end pass=%d fail=%d skip=%d\n", SHADER_SUITE_VERSION, pass_count, fail_count,
                skip_count);
        return 1;
    }

    code = compile("bad", "unknown-profile", vs_source, "xx_9_9", 0, FALSE);
    if (code) ID3D10Blob_Release(code);
    code = compile("bad", "syntax-error", "float4 main( : SV_Target { return 0; }", "ps_5_0", 0, FALSE);
    if (code) ID3D10Blob_Release(code);

    test_vertex_shader();
    test_pixel_shader();
    test_compute_shader();
    test_profile_matrix();
    printf("SHADER-SUITE v%d end pass=%d fail=%d skip=%d\n", SHADER_SUITE_VERSION, pass_count, fail_count,
            skip_count);
    return fail_count ? 1 : 0;
}
