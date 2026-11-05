/*
 * LiberX contract fixture: IDXGIDevice GPU thread priority round-trip.
 *
 * IDXGIDevice::Set/GetGPUThreadPriority previously returned E_NOTIMPL (a stub).
 * The documented contract is: the priority is validated to [-7, 7] (E_INVALIDARG
 * otherwise) and the accepted value round-trips through Get. This fixture creates
 * a D3D11 device, obtains its IDXGIDevice, and checks the contract. It SKIPs (not
 * PASS) when no device can be created, and never asserts fake success.
 */
#define COBJMACROS
#define INITGUID
#define CONST_VTABLE
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <d3d11.h>
#include <dxgi.h>

#define TEST_VERSION 1
static int fail_count, skip_count, pass_count;

static void report(const char *name, const char *status, const char *fmt, ...)
{
    char d[256]; va_list a; va_start(a, fmt); vsnprintf(d, sizeof(d), fmt, a); va_end(a);
    printf("DXGI_PRIORITY v%d %s %s %s\n", TEST_VERSION, name, status, d);
    if (status[0]=='F') ++fail_count; else if (status[0]=='S') ++skip_count; else ++pass_count;
}
static void check(const char *name, BOOL ok, const char *fmt, ...)
{
    char d[256]; va_list a; va_start(a, fmt); vsnprintf(d, sizeof(d), fmt, a); va_end(a);
    report(name, ok?"PASS":"FAIL", "%s", d);
}

int main(void)
{
    ID3D11Device *dev = NULL;
    IDXGIDevice *dxgi = NULL;
    HRESULT hr;
    INT p;
    static const D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, fl, 2,
                           D3D11_SDK_VERSION, &dev, NULL, NULL);
    if (FAILED(hr) || !dev)
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, fl, 2,
                               D3D11_SDK_VERSION, &dev, NULL, NULL);
    if (FAILED(hr) || !dev)
    {
        report("create_device", "SKIP", "D3D11CreateDevice hr=0x%08lx", hr);
        goto done;
    }

    hr = ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice, (void **)&dxgi);
    if (FAILED(hr) || !dxgi)
    {
        report("qi_dxgidevice", "SKIP", "QI IDXGIDevice hr=0x%08lx", hr);
        goto done;
    }

    /* valid range round-trips */
    hr = IDXGIDevice_SetGPUThreadPriority(dxgi, 5);
    check("set_valid", SUCCEEDED(hr), "Set(5) hr=0x%08lx", hr);
    p = 0x7FFFFFFF;
    hr = IDXGIDevice_GetGPUThreadPriority(dxgi, &p);
    check("get_roundtrip", SUCCEEDED(hr) && p == 5, "Get hr=0x%08lx p=%d (expect 5)", hr, p);

    hr = IDXGIDevice_SetGPUThreadPriority(dxgi, -7);
    check("set_min", SUCCEEDED(hr), "Set(-7) hr=0x%08lx", hr);
    hr = IDXGIDevice_GetGPUThreadPriority(dxgi, &p);
    check("get_min", SUCCEEDED(hr) && p == -7, "Get hr=0x%08lx p=%d (expect -7)", hr, p);

    /* out of range rejected */
    hr = IDXGIDevice_SetGPUThreadPriority(dxgi, 8);
    check("set_too_high", hr == E_INVALIDARG, "Set(8) hr=0x%08lx (expect E_INVALIDARG)", hr);
    hr = IDXGIDevice_SetGPUThreadPriority(dxgi, -8);
    check("set_too_low", hr == E_INVALIDARG, "Set(-8) hr=0x%08lx (expect E_INVALIDARG)", hr);

    /* NULL out rejected */
    hr = IDXGIDevice_GetGPUThreadPriority(dxgi, NULL);
    check("get_null", hr == E_INVALIDARG, "Get(NULL) hr=0x%08lx (expect E_INVALIDARG)", hr);

done:
    if (dxgi) IDXGIDevice_Release(dxgi);
    if (dev) ID3D11Device_Release(dev);
    printf("DXGI_PRIORITY v%d SUMMARY pass=%d fail=%d skip=%d\n",
           TEST_VERSION, pass_count, fail_count, skip_count);
    return fail_count;
}
