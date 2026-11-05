#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <initguid.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <string.h>
#include <dxgi.h>
#include <dwrite.h>

#define CONTRACT_VERSION 2
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

DEFINE_GUID(iid_dxgi_factory1, 0x770aae78, 0xf26f, 0x4dba, 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87);
DEFINE_GUID(iid_dwrite_factory, 0xb859ee5a, 0xd838, 0x4b5b, 0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48);
DEFINE_GUID(iid_d2d_factory, 0x06152247, 0x6f50, 0x465a, 0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07);
DEFINE_GUID(iid_d3d12_device, 0x189819f1, 0x1db6, 0x4b57, 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7);
DEFINE_GUID(iid_d3d12_root_signature, 0xc54a6b66, 0x72df, 0x4ee8, 0x8b, 0xe5, 0xa9, 0x46, 0xa1, 0x42, 0x92, 0x14);
DEFINE_GUID(iid_d3d12_pipeline_state, 0x765a30f3, 0xf624, 0x4c6f, 0xa8, 0x28, 0xac, 0xe9, 0x48, 0x62, 0x24, 0x45);
DEFINE_GUID(iid_unknown_probe, 0x5b7a1c1e, 0x9d3a, 0x4a52, 0x8f, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

#define ASSERT_SLOT(name, vtbl, member, index) \
    typedef char slot_##name[(offsetof(vtbl, member) / sizeof(void *) == (index)) ? 1 : -1]

ASSERT_SLOT(dxgi_object_get_parent, IDXGIObjectVtbl, GetParent, 6);
ASSERT_SLOT(dxgi_factory_enum_adapters, IDXGIFactoryVtbl, EnumAdapters, 7);
ASSERT_SLOT(dxgi_factory_create_swap_chain, IDXGIFactoryVtbl, CreateSwapChain, 10);
ASSERT_SLOT(dxgi_factory1_enum_adapters1, IDXGIFactory1Vtbl, EnumAdapters1, 12);
ASSERT_SLOT(d3d12_object_set_name, ID3D12ObjectVtbl, SetName, 6);
ASSERT_SLOT(d3d12_device_get_node_count, ID3D12DeviceVtbl, GetNodeCount, 7);
ASSERT_SLOT(d3d12_device_create_command_queue, ID3D12DeviceVtbl, CreateCommandQueue, 8);
ASSERT_SLOT(dwrite_factory_get_system_font_collection, IDWriteFactoryVtbl, GetSystemFontCollection, 3);

static int fail_count, skip_count, pass_count;

static void report(const char *dll, const char *name, const char *status, const char *fmt, ...)
{
    char detail[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    printf("CONTRACT v%d %s %s %s %s\n", CONTRACT_VERSION, dll, name, status, detail);
    if (status[0] == 'F') ++fail_count;
    else if (status[0] == 'S') ++skip_count;
    else ++pass_count;
}

static void check(const char *dll, const char *name, BOOL ok, const char *fmt, ...)
{
    char detail[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    report(dll, name, ok ? "PASS" : "FAIL", "%s", detail);
}

static const struct { const char *dll; const char *exports[8]; } export_table[] =
{
    {"d3d11.dll", {"D3D11CreateDevice", "D3D11CreateDeviceAndSwapChain"}},
    {"d3d12.dll", {"D3D12CreateDevice", "D3D12GetDebugInterface", "D3D12SerializeRootSignature",
            "D3D12CreateRootSignatureDeserializer", "D3D12EnableExperimentalFeatures"}},
    {"dxgi.dll", {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2"}},
    {"dxcore.dll", {"DXCoreCreateAdapterFactory"}},
    {"d2d1.dll", {"D2D1CreateFactory", "D2D1CreateDevice"}},
    {"dwrite.dll", {"DWriteCreateFactory"}},
    {"d3d9.dll", {"Direct3DCreate9", "Direct3DCreate9Ex"}},
    {"ddraw.dll", {"DirectDrawCreate", "DirectDrawCreateEx"}},
    {"dsound.dll", {"DirectSoundCreate", "DirectSoundCreate8"}},
    {"dinput8.dll", {"DirectInput8Create"}},
    {"xinput1_4.dll", {"XInputGetState", "XInputSetState", "XInputGetCapabilities"}},
    {"d3dcompiler_47.dll", {"D3DCompile", "D3DReflect"}},
};

static void test_exports(void)
{
    unsigned int i, j;

    for (i = 0; i < ARRAY_SIZE(export_table); ++i)
    {
        HMODULE module = LoadLibraryA(export_table[i].dll);

        if (!module)
        {
            report(export_table[i].dll, "load", "FAIL", "error %lu", GetLastError());
            continue;
        }
        for (j = 0; j < ARRAY_SIZE(export_table[i].exports) && export_table[i].exports[j]; ++j)
            check(export_table[i].dll, export_table[i].exports[j], !!GetProcAddress(module, export_table[i].exports[j]),
                    "export");
    }
}

typedef HRESULT (WINAPI *create_dxgi_func)(REFIID, void **);
typedef HRESULT (WINAPI *create_dwrite_func)(UINT, REFIID, IUnknown **);
typedef HRESULT (WINAPI *create_d2d_func)(UINT, REFIID, const void *, void **);
typedef HRESULT (WINAPI *create_d3d12_func)(IUnknown *, UINT, REFIID, void **);
typedef HRESULT (WINAPI *create_d3d11_func)(void *, UINT, HMODULE, UINT, const UINT *, UINT, UINT, void **,
        UINT *, void **);

static void test_unknown_contract(const char *dll, const char *what, IUnknown *object)
{
    IUnknown *other = (IUnknown *)(ULONG_PTR)1;
    IUnknown *same = NULL;
    ULONG ref;
    HRESULT hr;

    hr = IUnknown_QueryInterface(object, &iid_unknown_probe, (void **)&other);
    check(dll, what, hr == E_NOINTERFACE && !other, "QI unknown iid: hr=%#lx out=%p", hr, other);

    hr = IUnknown_QueryInterface(object, &IID_IUnknown, (void **)&same);
    check(dll, what, hr == S_OK && same, "QI IUnknown: hr=%#lx", hr);
    if (same)
    {
        ref = IUnknown_AddRef(object);
        check(dll, what, ref >= 3, "AddRef after QI: %lu", ref);
        IUnknown_Release(object);
        IUnknown_Release(same);
    }
}

static void test_dxgi(void)
{
    create_dxgi_func create;
    IUnknown *factory = (IUnknown *)(ULONG_PTR)1;
    HMODULE module = LoadLibraryA("dxgi.dll");
    HRESULT hr;

    if (!module || !(create = (create_dxgi_func)GetProcAddress(module, "CreateDXGIFactory1")))
    {
        report("dxgi.dll", "factory", "SKIP", "dxgi.dll or CreateDXGIFactory1 missing");
        return;
    }

    hr = create(&iid_unknown_probe, (void **)&factory);
    check("dxgi.dll", "factory-bad-iid", FAILED(hr) && !factory, "hr=%#lx out=%p", hr, factory);

    factory = NULL;
    hr = create(&iid_dxgi_factory1, (void **)&factory);
    if (FAILED(hr))
    {
        report("dxgi.dll", "factory", "FAIL", "hr=%#lx", hr);
        return;
    }
    test_unknown_contract("dxgi.dll", "factory", factory);
    IUnknown_Release(factory);
}

static void test_dwrite(void)
{
    create_dwrite_func create;
    IUnknown *factory = (IUnknown *)(ULONG_PTR)1;
    HMODULE module = LoadLibraryA("dwrite.dll");
    HRESULT hr;

    if (!module || !(create = (create_dwrite_func)GetProcAddress(module, "DWriteCreateFactory")))
    {
        report("dwrite.dll", "factory", "SKIP", "dwrite.dll or DWriteCreateFactory missing");
        return;
    }

    hr = create(99, &iid_dwrite_factory, &factory);
    check("dwrite.dll", "factory-bad-type", FAILED(hr) && !factory, "hr=%#lx out=%p", hr, factory);

    factory = NULL;
    hr = create(DWRITE_FACTORY_TYPE_SHARED, &iid_dwrite_factory, &factory);
    if (FAILED(hr))
    {
        report("dwrite.dll", "factory", "FAIL", "hr=%#lx", hr);
        return;
    }
    test_unknown_contract("dwrite.dll", "factory", factory);
    IUnknown_Release(factory);
}

static void test_d2d1(void)
{
    create_d2d_func create;
    IUnknown *factory = (IUnknown *)(ULONG_PTR)1;
    HMODULE module = LoadLibraryA("d2d1.dll");
    HRESULT hr;

    if (!module || !(create = (create_d2d_func)GetProcAddress(module, "D2D1CreateFactory")))
    {
        report("d2d1.dll", "factory", "SKIP", "d2d1.dll or D2D1CreateFactory missing");
        return;
    }

    hr = create(0, &iid_unknown_probe, NULL, (void **)&factory);
    check("d2d1.dll", "factory-bad-iid", FAILED(hr) && !factory, "hr=%#lx out=%p", hr, factory);

    factory = NULL;
    hr = create(0, &iid_d2d_factory, NULL, (void **)&factory);
    if (FAILED(hr))
    {
        report("d2d1.dll", "factory", "FAIL", "hr=%#lx", hr);
        return;
    }
    test_unknown_contract("d2d1.dll", "factory", factory);
    IUnknown_Release(factory);
}

typedef HRESULT (WINAPI *serialize_root_signature_func)(const D3D12_ROOT_SIGNATURE_DESC *,
        D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);

static ID3DBlob *compile_shader(pD3DCompile compile, const char *source, const char *target)
{
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = compile(source, strlen(source), NULL, NULL, NULL, "main", target, 0, 0, &code, &errors);

    if (errors)
        ID3D10Blob_Release(errors);
    return SUCCEEDED(hr) ? code : NULL;
}

/* Roadmap item 10: the conservative rasterization tier and ROV support must be
 * what the backend really has; feature level 12_1 may only be reported when both
 * are; and a pipeline asking for conservative rasterization must fail cleanly on
 * a tier 0 device and be accepted on any other. */
static void test_d3d12_raster_tiers(ID3D12Device *device, HMODULE d3d12)
{
    static const D3D_FEATURE_LEVEL levels[] =
            {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1};
    static const char vs_source[] =
            "float4 main(uint id : SV_VertexID) : SV_Position { return float4(0.0, 0.0, 0.0, 1.0); }";
    static const char ps_source[] =
            "float4 main() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }";
    ID3DBlob *vs = NULL, *ps = NULL, *rs_blob = NULL, *rs_errors = NULL;
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    ID3D12RootSignature *root_signature = NULL;
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl;
    serialize_root_signature_func serialize;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12PipelineState *pso = NULL;
    pD3DCompile compile;
    unsigned int tier;
    HRESULT hr;

    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    check("d3d12.dll", "options-query", hr == S_OK, "hr=%#lx", hr);
    if (FAILED(hr))
        return;
    tier = options.ConservativeRasterizationTier;
    check("d3d12.dll", "crast-tier-range", tier <= D3D12_CONSERVATIVE_RASTERIZATION_TIER_3, "tier=%u", tier);
    check("d3d12.dll", "rov-bool", options.ROVsSupported == TRUE || options.ROVsSupported == FALSE,
            "rov=%d", options.ROVsSupported);

    fl.NumFeatureLevels = ARRAY_SIZE(levels);
    fl.pFeatureLevelsRequested = levels;
    fl.MaxSupportedFeatureLevel = 0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl));
    check("d3d12.dll", "fl-12_1-honest", SUCCEEDED(hr) && (fl.MaxSupportedFeatureLevel < D3D_FEATURE_LEVEL_12_1
            || (options.ROVsSupported && tier >= D3D12_CONSERVATIVE_RASTERIZATION_TIER_1)),
            "hr=%#lx max=%#x rov=%d tier=%u", hr, fl.MaxSupportedFeatureLevel, options.ROVsSupported, tier);

    compile = compiler ? (pD3DCompile)GetProcAddress(compiler, "D3DCompile") : NULL;
    serialize = (serialize_root_signature_func)GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    if (!compile || !serialize || !(vs = compile_shader(compile, vs_source, "vs_5_0"))
            || !(ps = compile_shader(compile, ps_source, "ps_5_0")))
    {
        report("d3d12.dll", "crast-pso", "SKIP", "no D3DCompile/D3D12SerializeRootSignature or shaders failed");
        goto done;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    hr = serialize(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_errors);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(rs_blob),
                ID3D10Blob_GetBufferSize(rs_blob), &iid_d3d12_root_signature, (void **)&root_signature);
    if (FAILED(hr))
    {
        report("d3d12.dll", "crast-pso", "SKIP", "root signature hr=%#lx (not a tier result)", hr);
        goto done;
    }

    memset(&desc, 0, sizeof(desc));
    desc.pRootSignature = root_signature;
    desc.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs);
    desc.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    desc.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps);
    desc.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = ~0u;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

    hr = ID3D12Device_CreateGraphicsPipelineState(device, &desc, &iid_d3d12_pipeline_state, (void **)&pso);
    if (FAILED(hr) || !pso)
    {
        report("d3d12.dll", "crast-pso", "SKIP", "baseline pipeline hr=%#lx (not a tier result)", hr);
        goto done;
    }
    ID3D12PipelineState_Release(pso);
    pso = NULL;

    desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &desc, &iid_d3d12_pipeline_state, (void **)&pso);
    if (tier == D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED)
        check("d3d12.dll", "crast-pso-tier0-rejected", FAILED(hr) && !pso, "hr=%#lx out=%p", hr, pso);
    else
        check("d3d12.dll", "crast-pso-accepted", SUCCEEDED(hr) && pso, "tier=%u hr=%#lx out=%p", tier, hr, pso);
    if (pso)
        ID3D12PipelineState_Release(pso);

done:
    if (root_signature)
        ID3D12RootSignature_Release(root_signature);
    if (rs_blob)
        ID3D10Blob_Release(rs_blob);
    if (rs_errors)
        ID3D10Blob_Release(rs_errors);
    if (vs)
        ID3D10Blob_Release(vs);
    if (ps)
        ID3D10Blob_Release(ps);
    if (compiler)
        FreeLibrary(compiler);
}

static void test_d3d12(void)
{
    create_d3d12_func create;
    IUnknown *device = (IUnknown *)(ULONG_PTR)1;
    HMODULE module = LoadLibraryA("d3d12.dll");
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    HRESULT hr;

    if (!module || !(create = (create_d3d12_func)GetProcAddress(module, "D3D12CreateDevice")))
    {
        report("d3d12.dll", "device", "SKIP", "d3d12.dll or D3D12CreateDevice missing");
        return;
    }

    hr = create(NULL, 0x1, &iid_d3d12_device, (void **)&device);
    check("d3d12.dll", "device-bad-level", FAILED(hr) && !device, "hr=%#lx out=%p", hr, device);

    device = NULL;
    hr = create(NULL, D3D_FEATURE_LEVEL_11_0, &iid_d3d12_device, (void **)&device);
    if (FAILED(hr))
    {
        report("d3d12.dll", "device", "SKIP", "no device at 11_0, hr=%#lx (not a contract result)", hr);
        return;
    }

    test_unknown_contract("d3d12.dll", "device", device);
    hr = ID3D12Device_CheckFeatureSupport((ID3D12Device *)device, D3D12_FEATURE_D3D12_OPTIONS, &options,
            sizeof(options) - 1);
    check("d3d12.dll", "options-bad-size", hr == E_INVALIDARG, "hr=%#lx", hr);
    hr = ID3D12Device_CheckFeatureSupport((ID3D12Device *)device, (D3D12_FEATURE)0x7fff, &options, sizeof(options));
    check("d3d12.dll", "options-unknown-feature", FAILED(hr), "hr=%#lx", hr);
    test_d3d12_raster_tiers((ID3D12Device *)device, module);
    IUnknown_Release(device);
}

static void test_d3d11(void)
{
    create_d3d11_func create;
    void *device = (void *)(ULONG_PTR)1;
    HMODULE module = LoadLibraryA("d3d11.dll");
    HRESULT hr;

    if (!module || !(create = (create_d3d11_func)GetProcAddress(module, "D3D11CreateDevice")))
    {
        report("d3d11.dll", "device", "SKIP", "d3d11.dll or D3D11CreateDevice missing");
        return;
    }

    hr = create(NULL, 99, NULL, 0, NULL, 0, 7, &device, NULL, NULL);
    check("d3d11.dll", "device-bad-driver-type", FAILED(hr) && !device, "hr=%#lx out=%p", hr, device);
}

static void test_xinput(void)
{
    typedef DWORD (WINAPI *get_state_func)(DWORD, void *);
    get_state_func get_state;
    HMODULE module = LoadLibraryA("xinput1_4.dll");
    BYTE state[16];
    DWORD ret;

    if (!module || !(get_state = (get_state_func)GetProcAddress(module, "XInputGetState")))
    {
        report("xinput1_4.dll", "get-state", "SKIP", "xinput1_4.dll or XInputGetState missing");
        return;
    }
    ret = get_state(99, state);
    check("xinput1_4.dll", "get-state-bad-index", ret != ERROR_SUCCESS, "ret=%lu", ret);
}

int main(void)
{
    printf("CONTRACT-SUITE v%d begin\n", CONTRACT_VERSION);
    test_exports();
    test_dxgi();
    test_dwrite();
    test_d2d1();
    test_d3d12();
    test_d3d11();
    test_xinput();
    printf("CONTRACT-SUITE v%d end pass=%d fail=%d skip=%d\n", CONTRACT_VERSION, pass_count, fail_count, skip_count);
    return fail_count ? 1 : 0;
}
