#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_3.h>
#include <d2derr.h>
#include <dxdiag.h>

#define SUITE_VERSION 1
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

DEFINE_GUID(liberx_iid_dxgi_device, 0x54ec77fa, 0x1377, 0x44e6, 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c);
DEFINE_GUID(liberx_iid_d2d_factory1, 0xbb12d362, 0xdaee, 0x4b9a, 0xaa, 0x1d, 0x14, 0xba, 0x40, 0x1c, 0xfa, 0x1f);
DEFINE_GUID(liberx_iid_d2d_context5, 0x7836d248, 0x68cc, 0x4e5c, 0x9d, 0x5c, 0xe1, 0x91, 0x70, 0x9c, 0x5e, 0xbd);
DEFINE_GUID(liberx_clsid_dxdiag_provider, 0xa65b8071, 0x3bfe, 0x4213, 0x9a, 0x0b, 0x9f, 0x0d, 0x1e, 0xa0, 0x3a, 0x4c);
DEFINE_GUID(liberx_iid_dxdiag_provider, 0x9c6b4cb0, 0x23f8, 0x49cc, 0xa3, 0xed, 0x45, 0xa5, 0x50, 0x00, 0xa6, 0xd2);

static int pass_count, fail_count, skip_count;

static void report(const char *group, const char *name, const char *status, const char *fmt, ...)
{
    char detail[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    printf("ITEM1 v%d %s %s %s %s\n", SUITE_VERSION, group, name, status, detail);
    if (status[0] == 'F') ++fail_count;
    else if (status[0] == 'S') ++skip_count;
    else ++pass_count;
}

typedef HRESULT (WINAPI *create_d3d11_func)(void *, UINT, HMODULE, UINT, const UINT *, UINT, UINT, ID3D11Device **,
        UINT *, void **);
typedef HRESULT (WINAPI *create_d2d_factory_func)(UINT, REFIID, const void *, void **);

static void test_d2d_unsupported_draw(void)
{
    create_d3d11_func create_d3d11;
    create_d2d_factory_func create_factory;
    ID3D11Device *d3d_device = NULL;
    IDXGIDevice *dxgi_device = NULL;
    ID2D1Factory1 *factory = NULL;
    ID2D1Device *device = NULL;
    ID2D1DeviceContext *context = NULL;
    ID2D1DeviceContext5 *context5 = NULL;
    ID2D1Bitmap1 *target = NULL;
    D2D1_BITMAP_PROPERTIES1 props;
    D2D1_TAG tag1, tag2;
    HMODULE d3d11 = LoadLibraryA("d3d11.dll"), d2d1 = LoadLibraryA("d2d1.dll");
    UINT level;
    HRESULT hr;

    if (!d3d11 || !d2d1
            || !(create_d3d11 = (create_d3d11_func)GetProcAddress(d3d11, "D3D11CreateDevice"))
            || !(create_factory = (create_d2d_factory_func)GetProcAddress(d2d1, "D2D1CreateFactory")))
    {
        report("d2d", "setup", "SKIP", "d3d11.dll, d2d1.dll or their exports missing");
        return;
    }

    hr = create_d3d11(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, 7, &d3d_device,
            &level, NULL);
    if (FAILED(hr) || !d3d_device)
    {
        report("d2d", "setup", "SKIP", "no D3D11 device, hr=%#lx (not a result for this item)", hr);
        return;
    }
    if (FAILED(hr = ID3D11Device_QueryInterface(d3d_device, &liberx_iid_dxgi_device, (void **)&dxgi_device))
            || FAILED(hr = create_factory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &liberx_iid_d2d_factory1, NULL,
                    (void **)&factory))
            || FAILED(hr = ID2D1Factory1_CreateDevice(factory, dxgi_device, &device))
            || FAILED(hr = ID2D1Device_CreateDeviceContext(device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)))
    {
        report("d2d", "setup", "SKIP", "D2D device context not available, hr=%#lx", hr);
        goto done;
    }

    memset(&props, 0, sizeof(props));
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = props.dpiY = 96.0f;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    {
        D2D1_SIZE_U size = {16, 16};

        hr = ID2D1DeviceContext_CreateBitmap(context, size, NULL, 0, &props, &target);
    }
    if (FAILED(hr))
    {
        report("d2d", "setup", "SKIP", "no target bitmap, hr=%#lx", hr);
        goto done;
    }
    ID2D1DeviceContext_SetTarget(context, (ID2D1Image *)target);

    hr = ID2D1DeviceContext_QueryInterface(context, &liberx_iid_d2d_context5, (void **)&context5);
    if (FAILED(hr))
    {
        report("d2d", "context5", "SKIP", "ID2D1DeviceContext5 not available, hr=%#lx", hr);
        goto done;
    }

    ID2D1DeviceContext_BeginDraw(context);
    ID2D1DeviceContext5_DrawSvgDocument(context5, NULL);
    tag1 = tag2 = 0;
    hr = ID2D1DeviceContext_Flush(context, &tag1, &tag2);
    report("d2d", "flush-reports-unsupported-draw", hr == D2DERR_UNSUPPORTED_OPERATION ? "PASS" : "FAIL",
            "Flush hr=%#lx, expected %#lx", hr, (long)D2DERR_UNSUPPORTED_OPERATION);
    hr = ID2D1DeviceContext_EndDraw(context, &tag1, &tag2);
    report("d2d", "enddraw-reports-unsupported-draw", hr == D2DERR_UNSUPPORTED_OPERATION ? "PASS" : "FAIL",
            "EndDraw hr=%#lx, expected %#lx", hr, (long)D2DERR_UNSUPPORTED_OPERATION);

    ID2D1DeviceContext_BeginDraw(context);
    hr = ID2D1DeviceContext_EndDraw(context, &tag1, &tag2);
    report("d2d", "clean-batch-succeeds", hr == S_OK ? "PASS" : "FAIL", "empty BeginDraw/EndDraw hr=%#lx", hr);

done:
    if (context5) ID2D1DeviceContext5_Release(context5);
    if (target) ID2D1Bitmap1_Release(target);
    if (context) ID2D1DeviceContext_Release(context);
    if (device) ID2D1Device_Release(device);
    if (factory) ID2D1Factory1_Release(factory);
    if (dxgi_device) IDXGIDevice_Release(dxgi_device);
    if (d3d_device) ID3D11Device_Release(d3d_device);
}

static BOOL get_property(IDxDiagContainer *container, const WCHAR *name, WCHAR *out, DWORD size, HRESULT *hr_out)
{
    VARIANT var;
    HRESULT hr;

    VariantInit(&var);
    hr = IDxDiagContainer_GetProp(container, name, &var);
    *hr_out = hr;
    if (FAILED(hr) || V_VT(&var) != VT_BSTR || !V_BSTR(&var))
    {
        VariantClear(&var);
        return FALSE;
    }
    wcsncpy(out, V_BSTR(&var), size - 1);
    out[size - 1] = 0;
    VariantClear(&var);
    return TRUE;
}

static void test_dxdiag_probe(void)
{
    typedef HRESULT (WINAPI *get_class_object_func)(REFCLSID, REFIID, void **);
    get_class_object_func get_class_object;
    IClassFactory *class_factory = NULL;
    IDxDiagProvider *provider = NULL;
    IDxDiagContainer *root = NULL, *system = NULL;
    DXDIAG_INIT_PARAMS init;
    HMODULE module = LoadLibraryA("dxdiagn.dll");
    WCHAR text[300];
    HRESULT hr;

    if (!module || !(get_class_object = (get_class_object_func)GetProcAddress(module, "DllGetClassObject")))
    {
        report("dxdiag", "setup", "SKIP", "dxdiagn.dll or DllGetClassObject missing");
        return;
    }
    if (FAILED(hr = get_class_object(&liberx_clsid_dxdiag_provider, &IID_IClassFactory, (void **)&class_factory))
            || FAILED(hr = IClassFactory_CreateInstance(class_factory, NULL, &liberx_iid_dxdiag_provider,
                    (void **)&provider)))
    {
        report("dxdiag", "setup", "SKIP", "provider not created, hr=%#lx", hr);
        goto done;
    }
    memset(&init, 0, sizeof(init));
    init.dwSize = sizeof(init);
    init.dwDxDiagHeaderVersion = DXDIAG_DX9_SDK_VERSION;
    init.bAllowWHQLChecks = FALSE;
    if (FAILED(hr = IDxDiagProvider_Initialize(provider, &init))
            || FAILED(hr = IDxDiagProvider_GetRootContainer(provider, &root))
            || FAILED(hr = IDxDiagContainer_GetChildContainer(root, L"DxDiag_SystemInfo", &system)))
    {
        report("dxdiag", "setup", "SKIP", "system info container not available, hr=%#lx", hr);
        goto done;
    }

    report("dxdiag", "legacy-version-kept",
            get_property(system, L"szDirectXVersionLongEnglish", text, ARRAY_SIZE(text), &hr) ? "PASS" : "FAIL",
            "szDirectXVersionLongEnglish hr=%#lx", hr);
    report("dxdiag", "version-note-present",
            get_property(system, L"szDirectXVersionNote", text, ARRAY_SIZE(text), &hr) ? "PASS" : "FAIL",
            "hr=%#lx", hr);
    if (get_property(system, L"szLiberXD3D11Probe", text, ARRAY_SIZE(text), &hr))
    {
        report("dxdiag", "d3d11-probe-present", "PASS", "%ls", text);
        report("dxdiag", "d3d11-probe-names-driver-types",
                (wcsstr(text, L"HARDWARE") && wcsstr(text, L"WARP") && wcsstr(text, L"REFERENCE")) ? "PASS" : "FAIL",
                "HARDWARE, WARP and REFERENCE must each be reported separately");
    }
    else
        report("dxdiag", "d3d11-probe-present", "FAIL", "hr=%#lx", hr);
    if (get_property(system, L"szLiberXD3D12Probe", text, ARRAY_SIZE(text), &hr))
        report("dxdiag", "d3d12-probe-present", "PASS", "%ls", text);
    else
        report("dxdiag", "d3d12-probe-present", "FAIL", "hr=%#lx", hr);

done:
    if (system) IDxDiagContainer_Release(system);
    if (root) IDxDiagContainer_Release(root);
    if (provider) IDxDiagProvider_Release(provider);
    if (class_factory) IClassFactory_Release(class_factory);
}

int main(void)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("ITEM1-SUITE v%d begin\n", SUITE_VERSION);
    test_d2d_unsupported_draw();
    test_dxdiag_probe();
    printf("ITEM1-SUITE v%d end pass=%d fail=%d skip=%d\n", SUITE_VERSION, pass_count, fail_count, skip_count);
    return fail_count ? 1 : 0;
}
