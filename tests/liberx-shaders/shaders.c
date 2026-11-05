#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>

#define SHADER_SUITE_VERSION 2
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


/* Item 9, second slice: a profile matrix whose EXPECTED FACTS were derived from
 * Microsoft's fxc.exe (Windows SDK 10.0.26100.0) on the same HLSL: whether the
 * profile accepts the source, the resource bindings, the input and output
 * signatures, the constant buffer layouts and the thread group size. The HLSL
 * and the facts are ours; no foreign bytecode is involved. Instruction counts
 * are compiler-specific and are not compared. */
static const char oracle_vs_cbuffer[] =
        "cbuffer Transform : register(b0)\n"
        "{\n"
        "    float4x4 mvp;\n"
        "    float2 offset;\n"
        "    float pad;\n"
        "};\n"
        "\n"
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };\n"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };\n"
        "\n"
        "VSOut main(VSIn i)\n"
        "{\n"
        "    VSOut o;\n"
        "    o.pos = mul(mvp, float4(i.pos + float3(offset, pad), 1.0));\n"
        "    o.uv = i.uv;\n"
        "    o.color = i.color;\n"
        "    return o;\n"
        "}\n";

static const char oracle_ps_textures[] =
        "Texture2D<float4> tex : register(t0);\n"
        "SamplerState samp : register(s0);\n"
        "Texture2D<float> mask : register(t3);\n"
        "\n"
        "cbuffer Tint : register(b1)\n"
        "{\n"
        "    float4 tint;\n"
        "};\n"
        "\n"
        "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
        "{\n"
        "    return tex.Sample(samp, uv) * tint * mask.Load(int3(pos.xy, 0));\n"
        "}\n";

static const char oracle_cs_structured[] =
        "struct Item { float4 value; uint id; };\n"
        "\n"
        "StructuredBuffer<Item> src : register(t0);\n"
        "RWStructuredBuffer<Item> dst : register(u0);\n"
        "RWTexture2D<float4> img : register(u1);\n"
        "\n"
        "[numthreads(8, 8, 1)]\n"
        "void main(uint3 tid : SV_DispatchThreadID)\n"
        "{\n"
        "    Item it = src[tid.x];\n"
        "    it.id += tid.y;\n"
        "    dst[tid.x] = it;\n"
        "    img[tid.xy] = it.value;\n"
        "}\n";

static const char oracle_ps_uav[] =
        "RWStructuredBuffer<uint> counter : register(u1);\n"
        "\n"
        "float4 main(float4 pos : SV_Position) : SV_Target\n"
        "{\n"
        "    counter[0] = (uint)pos.x;\n"
        "    return float4(1.0, 0.0, 0.0, 1.0);\n"
        "}\n";

struct oracle_case
{
    const char *fixture;
    const char *profile;
    BOOL compiles;
    const char *bindings;   /* name|class|bind point|count; ... */
    const char *inputs;     /* semantic|index|register|mask|sysval; ... */
    const char *outputs;
    const char *cbuffers;   /* name|size:var|offset|size,...; ... */
    const char *tgs;        /* x,y,z */
};

static const struct { const char *name; const char *source; } oracle_fixtures[] =
{
    {"vs_cbuffer", oracle_vs_cbuffer}, {"ps_textures", oracle_ps_textures},
    {"cs_structured", oracle_cs_structured}, {"ps_uav", oracle_ps_uav},
};

static const struct oracle_case oracle_matrix[] =
{
/* generated by sh_oracle.py from fxc.exe 10.0.26100.0 (host oracle); facts only */
    {"vs_cbuffer", "vs_4_0", TRUE, "Transform|cbuffer|0|1", "POSITION|0|0|xyz|NONE;TEXCOORD|0|1|xy|NONE;COLOR|0|2|xyzw|NONE", "SV_Position|0|0|xyzw|POS;TEXCOORD|0|1|xy|NONE;COLOR|0|2|xyzw|NONE", "Transform|80:mvp|0|64,offset|64|8,pad|72|4", "0,0,0"},
    {"vs_cbuffer", "vs_4_1", TRUE, "Transform|cbuffer|0|1", "POSITION|0|0|xyz|NONE;TEXCOORD|0|1|xy|NONE;COLOR|0|2|xyzw|NONE", "SV_Position|0|0|xyzw|POS;TEXCOORD|0|1|xy|NONE;COLOR|0|2|xyzw|NONE", "Transform|80:mvp|0|64,offset|64|8,pad|72|4", "0,0,0"},
    {"vs_cbuffer", "vs_5_0", TRUE, "Transform|cbuffer|0|1", "POSITION|0|0|xyz|NONE;TEXCOORD|0|1|xy|NONE;COLOR|0|2|xyzw|NONE", "SV_Position|0|0|xyzw|POS;TEXCOORD|0|1|xy|NONE;COLOR|0|2|xyzw|NONE", "Transform|80:mvp|0|64,offset|64|8,pad|72|4", "0,0,0"},
    {"vs_cbuffer", "vs_5_1", TRUE, "Transform|cbuffer|0|1", "POSITION|0|0|xyz|NONE;TEXCOORD|0|1|xy|NONE;COLOR|0|2|xyzw|NONE", "SV_Position|0|0|xyzw|POS;TEXCOORD|0|1|xy|NONE;COLOR|0|2|xyzw|NONE", "Transform|80:mvp|0|64,offset|64|8,pad|72|4", "0,0,0"},
    {"ps_textures", "ps_4_0", TRUE, "samp|sampler|0|1;tex|texture|0|1;mask|texture|3|1;Tint|cbuffer|1|1", "SV_Position|0|0|xyzw|POS;TEXCOORD|0|1|xy|NONE", "SV_Target|0|0|xyzw|TARGET", "Tint|16:tint|0|16", "0,0,0"},
    {"ps_textures", "ps_4_1", TRUE, "samp|sampler|0|1;tex|texture|0|1;mask|texture|3|1;Tint|cbuffer|1|1", "SV_Position|0|0|xyzw|POS;TEXCOORD|0|1|xy|NONE", "SV_Target|0|0|xyzw|TARGET", "Tint|16:tint|0|16", "0,0,0"},
    {"ps_textures", "ps_5_0", TRUE, "samp|sampler|0|1;tex|texture|0|1;mask|texture|3|1;Tint|cbuffer|1|1", "SV_Position|0|0|xyzw|POS;TEXCOORD|0|1|xy|NONE", "SV_Target|0|0|xyzw|TARGET", "Tint|16:tint|0|16", "0,0,0"},
    {"ps_textures", "ps_5_1", TRUE, "samp|sampler|0|1;tex|texture|0|1;mask|texture|3|1;Tint|cbuffer|1|1", "SV_Position|0|0|xyzw|POS;TEXCOORD|0|1|xy|NONE", "SV_Target|0|0|xyzw|TARGET", "Tint|16:tint|0|16", "0,0,0"},
    {"cs_structured", "cs_4_0", FALSE, "", "", "", "", "8,8,1"}, /* fxc: error X4509: maximum UAV register index exceeded, target has 1 slots, manual bind to slot u1 failed */
    {"cs_structured", "cs_4_1", FALSE, "", "", "", "", "8,8,1"}, /* fxc: error X4509: maximum UAV register index exceeded, target has 1 slots, manual bind to slot u1 failed */
    {"cs_structured", "cs_5_0", TRUE, "src|texture|0|1;dst|uav|0|1;img|uav|1|1", "", "", "", "8,8,1"},
    {"cs_structured", "cs_5_1", TRUE, "src|texture|0|1;dst|uav|0|1;img|uav|1|1", "", "", "", "8,8,1"},
    {"ps_uav", "ps_4_0", FALSE, "", "", "", "", "0,0,0"}, /* fxc: error X4509: maximum UAV register index exceeded, target has 0 slots, manual bind to slot u1 failed */
    {"ps_uav", "ps_4_1", FALSE, "", "", "", "", "0,0,0"}, /* fxc: error X4509: maximum UAV register index exceeded, target has 0 slots, manual bind to slot u1 failed */
    {"ps_uav", "ps_5_0", TRUE, "counter|uav|1|1", "SV_Position|0|0|xyzw|POS", "SV_Target|0|0|xyzw|TARGET", "", "0,0,0"},
    {"ps_uav", "ps_5_1", TRUE, "counter|uav|1|1", "SV_Position|0|0|xyzw|POS", "SV_Target|0|0|xyzw|TARGET", "", "0,0,0"},
};

static const char *oracle_source(const char *fixture)
{
    unsigned int i;

    for (i = 0; i < COUNT_OF(oracle_fixtures); ++i)
        if (!strcmp(oracle_fixtures[i].name, fixture)) return oracle_fixtures[i].source;
    return NULL;
}

/* ';'-separated sets: equal when the counts match and every expected entry occurs. */
static unsigned int set_count(const char *set)
{
    unsigned int n = 0;

    if (!set || !*set) return 0;
    for (n = 1; *set; ++set)
        if (*set == ';') ++n;
    return n;
}

static BOOL set_has(const char *set, const char *entry, size_t len)
{
    const char *p = set;

    while (p && *p)
    {
        const char *end = strchr(p, ';');
        size_t n = end ? (size_t)(end - p) : strlen(p);

        if (n == len && !memcmp(p, entry, len)) return TRUE;
        p = end ? end + 1 : NULL;
    }
    return FALSE;
}

static BOOL sets_equal(const char *expected, const char *actual)
{
    const char *p = expected;

    if (set_count(expected) != set_count(actual)) return FALSE;
    while (p && *p)
    {
        const char *end = strchr(p, ';');
        size_t n = end ? (size_t)(end - p) : strlen(p);

        if (!set_has(actual, p, n)) return FALSE;
        p = end ? end + 1 : NULL;
    }
    return TRUE;
}

static void set_append(char *buf, size_t cap, const char *entry)
{
    size_t len = strlen(buf);

    if (len && len + 1 < cap) buf[len++] = ';';
    if (len < cap) strncpy(buf + len, entry, cap - len - 1);
    buf[cap - 1] = 0;
}

static const char *sit_class(D3D_SHADER_INPUT_TYPE type)
{
    switch (type)
    {
        case D3D_SIT_CBUFFER: return "cbuffer";
        case D3D_SIT_TBUFFER: return "tbuffer";
        case D3D_SIT_SAMPLER: return "sampler";
        case D3D_SIT_TEXTURE:
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS: return "texture";
        default: return "uav";
    }
}

static const char *sysval_name(D3D_NAME name)
{
    switch (name)
    {
        case D3D_NAME_UNDEFINED: return "NONE";
        case D3D_NAME_POSITION: return "POS";
        case D3D_NAME_CLIP_DISTANCE: return "CLIPDST";
        case D3D_NAME_CULL_DISTANCE: return "CULLDST";
        case D3D_NAME_RENDER_TARGET_ARRAY_INDEX: return "RTINDEX";
        case D3D_NAME_VIEWPORT_ARRAY_INDEX: return "VPINDEX";
        case D3D_NAME_VERTEX_ID: return "VERTID";
        case D3D_NAME_PRIMITIVE_ID: return "PRIMID";
        case D3D_NAME_INSTANCE_ID: return "INSTID";
        case D3D_NAME_IS_FRONT_FACE: return "FFACE";
        case D3D_NAME_SAMPLE_INDEX: return "SAMPLE";
        case D3D_NAME_TARGET: return "TARGET";
        case D3D_NAME_DEPTH: return "DEPTH";
        case D3D_NAME_COVERAGE: return "COVERAGE";
        case D3D_NAME_DEPTH_GREATER_EQUAL: return "DEPTHGE";
        case D3D_NAME_DEPTH_LESS_EQUAL: return "DEPTHLE";
        default: return "OTHER";
    }
}

static void mask_string(BYTE mask, char out[5])
{
    unsigned int i, n = 0;

    for (i = 0; i < 4; ++i)
        if (mask & (1u << i)) out[n++] = "xyzw"[i];
    out[n] = 0;
}

static void actual_bindings(ID3D11ShaderReflection *r, const D3D11_SHADER_DESC *desc, char *buf, size_t cap)
{
    D3D11_SHADER_INPUT_BIND_DESC b;
    char entry[160];
    unsigned int i;

    buf[0] = 0;
    for (i = 0; i < desc->BoundResources; ++i)
    {
        if (FAILED(r->lpVtbl->GetResourceBindingDesc(r, i, &b))) continue;
        snprintf(entry, sizeof(entry), "%s|%s|%u|%u", b.Name, sit_class(b.Type), b.BindPoint, b.BindCount);
        set_append(buf, cap, entry);
    }
}

static void actual_signature(ID3D11ShaderReflection *r, unsigned int count, BOOL output, char *buf, size_t cap)
{
    D3D11_SIGNATURE_PARAMETER_DESC p;
    char entry[160], mask[5];
    unsigned int i;
    HRESULT hr;

    buf[0] = 0;
    for (i = 0; i < count; ++i)
    {
        hr = output ? r->lpVtbl->GetOutputParameterDesc(r, i, &p) : r->lpVtbl->GetInputParameterDesc(r, i, &p);
        if (FAILED(hr)) continue;
        mask_string(p.Mask, mask);
        snprintf(entry, sizeof(entry), "%s|%u|%u|%s|%s", p.SemanticName, p.SemanticIndex, p.Register, mask,
                sysval_name(p.SystemValueType));
        set_append(buf, cap, entry);
    }
}

static void actual_cbuffers(ID3D11ShaderReflection *r, const D3D11_SHADER_DESC *desc, char *buf, size_t cap)
{
    ID3D11ShaderReflectionConstantBuffer *cb;
    ID3D11ShaderReflectionVariable *var;
    D3D11_SHADER_VARIABLE_DESC vd;
    D3D11_SHADER_BUFFER_DESC bd;
    char entry[256], item[96];
    unsigned int i, j;

    buf[0] = 0;
    for (i = 0; i < desc->ConstantBuffers; ++i)
    {
        if (!(cb = r->lpVtbl->GetConstantBufferByIndex(r, i)) || FAILED(cb->lpVtbl->GetDesc(cb, &bd))) continue;
        if (bd.Type != D3D_CT_CBUFFER) continue;
        snprintf(entry, sizeof(entry), "%s|%u:", bd.Name, bd.Size);
        for (j = 0; j < bd.Variables; ++j)
        {
            if (!(var = cb->lpVtbl->GetVariableByIndex(cb, j)) || FAILED(var->lpVtbl->GetDesc(var, &vd))) continue;
            snprintf(item, sizeof(item), "%s%s|%u|%u", j ? "," : "", vd.Name, vd.StartOffset, vd.Size);
            strncat(entry, item, sizeof(entry) - strlen(entry) - 1);
        }
        set_append(buf, cap, entry);
    }
}

static void test_oracle_matrix(void)
{
    char bindings[512], inputs[512], outputs[512], cbuffers[512], tgs[40], name[64];
    ID3D11ShaderReflection *r;
    D3D11_SHADER_DESC desc;
    UINT x, y, z;
    unsigned int i;

    for (i = 0; i < COUNT_OF(oracle_matrix); ++i)
    {
        const struct oracle_case *c = &oracle_matrix[i];
        const char *source = oracle_source(c->fixture);
        ID3DBlob *code;

        snprintf(name, sizeof(name), "%s-compile", c->profile);
        code = compile(c->fixture, name, source, c->profile, 0, c->compiles);
        if (!c->compiles || !code)
        {
            if (code) ID3D10Blob_Release(code);
            continue;
        }
        snprintf(name, sizeof(name), "%s-reflect", c->profile);
        if (!(r = reflect(c->fixture, name, code)))
        {
            ID3D10Blob_Release(code);
            continue;
        }
        if (FAILED(r->lpVtbl->GetDesc(r, &desc)))
        {
            report(c->fixture, name, "FAIL", "GetDesc failed");
            r->lpVtbl->Release(r);
            ID3D10Blob_Release(code);
            continue;
        }
        actual_bindings(r, &desc, bindings, sizeof(bindings));
        actual_signature(r, desc.InputParameters, FALSE, inputs, sizeof(inputs));
        actual_signature(r, desc.OutputParameters, TRUE, outputs, sizeof(outputs));
        actual_cbuffers(r, &desc, cbuffers, sizeof(cbuffers));
        x = y = z = 0;
        r->lpVtbl->GetThreadGroupSize(r, &x, &y, &z);
        snprintf(tgs, sizeof(tgs), "%u,%u,%u", x, y, z);

        snprintf(name, sizeof(name), "%s-bindings", c->profile);
        check(c->fixture, name, sets_equal(c->bindings, bindings), "exp=[%.100s] got=[%.100s]", c->bindings, bindings);
        snprintf(name, sizeof(name), "%s-inputs", c->profile);
        check(c->fixture, name, sets_equal(c->inputs, inputs), "exp=[%.100s] got=[%.100s]", c->inputs, inputs);
        snprintf(name, sizeof(name), "%s-outputs", c->profile);
        check(c->fixture, name, sets_equal(c->outputs, outputs), "exp=[%.100s] got=[%.100s]", c->outputs, outputs);
        snprintf(name, sizeof(name), "%s-cbuffers", c->profile);
        check(c->fixture, name, sets_equal(c->cbuffers, cbuffers), "exp=[%.100s] got=[%.100s]", c->cbuffers, cbuffers);
        snprintf(name, sizeof(name), "%s-threadgroup", c->profile);
        check(c->fixture, name, !strcmp(c->tgs, tgs), "exp=%s got=%s", c->tgs, tgs);
        r->lpVtbl->Release(r);
        ID3D10Blob_Release(code);
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
    test_oracle_matrix();
    printf("SHADER-SUITE v%d end pass=%d fail=%d skip=%d\n", SHADER_SUITE_VERSION, pass_count, fail_count,
            skip_count);
    return fail_count ? 1 : 0;
}
