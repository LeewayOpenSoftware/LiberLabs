/*
 * LiberX item 5: D3D9 / D3D9Ex / DDraw end-to-end render and 9Ex gap probe.
 * Console (win32cui). Pass/fail is by pixel readback where a surface is produced;
 * 9Ex query methods are judged against their documented success contract.
 * Output: one "LEGACY v1 <api> <check> PASS|FAIL|SKIP <detail>" line per check,
 * then a summary line; process exit code is the number of FAILs.
 * The DX DLLs are pulled in with LoadLibrary so a branch build next to the exe wins.
 */
#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <initguid.h>
#include <d3d9.h>
#include <ddraw.h>
#include "legacy.h"

int fail_count, skip_count, pass_count;

void report(const char *api, const char *name, const char *status, const char *fmt, ...)
{
    char detail[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    printf("LEGACY v%d %s %s %s %s\n", LEGACY_VERSION, api, name, status, detail);
    fflush(stdout);
    if (status[0] == 'F') ++fail_count;
    else if (status[0] == 'S') ++skip_count;
    else ++pass_count;
}

void check(const char *api, const char *name, BOOL ok, const char *fmt, ...)
{
    char detail[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    report(api, name, ok ? "PASS" : "FAIL", "%s", detail);
}

/* flat colours should match exactly; a small tolerance absorbs backend dithering. */
static BOOL argb_close(DWORD got, DWORD exp, int tol)
{
    int i;
    for (i = 0; i < 3; ++i)
    {
        int g = (got >> (i * 8)) & 0xff;
        int e = (exp >> (i * 8)) & 0xff;
        int d = g - e;
        if (d < 0) d = -d;
        if (d > tol) return FALSE;
    }
    return TRUE;
}

static DWORD pixel_at(const D3DLOCKED_RECT *lr, int x, int y)
{
    return *(DWORD *)((BYTE *)lr->pBits + y * lr->Pitch + x * 4);
}

#define WIN_W 256
#define WIN_H 256
#define CLEAR_RGB 0x00204080u
#define QUAD_RGB  0x00a01030u

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcW(h, m, w, l);
}

static HWND make_window(void)
{
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"liberx_legacy";
    RegisterClassW(&wc);
    return CreateWindowExW(0, L"liberx_legacy", L"LiberX legacy render",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                           WIN_W + 16, WIN_H + 39, NULL, NULL, wc.hInstance, NULL);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

typedef IDirect3D9 *(WINAPI *pDirect3DCreate9)(UINT);
typedef HRESULT (WINAPI *pDirect3DCreate9Ex)(UINT, IDirect3D9Ex **);

struct vtx { float x, y, z, rhw; DWORD color; };

/* Clear to CLEAR_RGB, draw a QUAD_RGB quad over the centre, read the lockable
 * back buffer back and verify both the cleared border and the drawn centre. */
static void d3d9_draw_and_readback(const char *api, IDirect3DDevice9 *dev)
{
    HRESULT hr;
    IDirect3DSurface9 *bb = NULL;
    D3DLOCKED_RECT lr;
    struct vtx quad[4] =
    {
        {  64.0f,  64.0f, 0.5f, 1.0f, QUAD_RGB },
        { 192.0f,  64.0f, 0.5f, 1.0f, QUAD_RGB },
        {  64.0f, 192.0f, 0.5f, 1.0f, QUAD_RGB },
        { 192.0f, 192.0f, 0.5f, 1.0f, QUAD_RGB },
    };

    hr = IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, CLEAR_RGB, 1.0f, 0);
    check(api, "clear", SUCCEEDED(hr), "hr=0x%08lx", hr);

    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    hr = IDirect3DDevice9_BeginScene(dev);
    if (SUCCEEDED(hr))
    {
        hr = IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
        IDirect3DDevice9_EndScene(dev);
    }
    check(api, "draw-quad", SUCCEEDED(hr), "hr=0x%08lx", hr);

    hr = IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if (FAILED(hr) || !bb)
    {
        report(api, "readback", "SKIP", "GetBackBuffer hr=0x%08lx", hr);
    }
    else
    {
        hr = IDirect3DSurface9_LockRect(bb, &lr, NULL, D3DLOCK_READONLY);
        if (FAILED(hr))
        {
            report(api, "readback", "SKIP", "LockRect hr=0x%08lx (back buffer not lockable)", hr);
        }
        else
        {
            DWORD border = pixel_at(&lr, 16, 16) & 0x00ffffff;
            DWORD centre = pixel_at(&lr, 128, 128) & 0x00ffffff;
            IDirect3DSurface9_UnlockRect(bb);
            check(api, "readback-clear", argb_close(border, CLEAR_RGB, 2),
                  "border=0x%06lx want 0x%06lx", border, CLEAR_RGB);
            check(api, "readback-draw", argb_close(centre, QUAD_RGB, 2),
                  "centre=0x%06lx want 0x%06lx", centre, QUAD_RGB);
        }
        IDirect3DSurface9_Release(bb);
    }

    hr = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
    check(api, "present", SUCCEEDED(hr), "hr=0x%08lx", hr);
    pump();
    Sleep(400);
}

static void run_d3d9(HWND hwnd, HMODULE mod)
{
    pDirect3DCreate9 create;
    IDirect3D9 *d3d;
    IDirect3DDevice9 *dev = NULL;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;

    create = (pDirect3DCreate9)GetProcAddress(mod, "Direct3DCreate9");
    if (!create) { report("d3d9", "create-fn", "FAIL", "no Direct3DCreate9 export"); return; }
    d3d = create(D3D_SDK_VERSION);
    if (!d3d) { report("d3d9", "Direct3DCreate9", "FAIL", "returned NULL"); return; }
    report("d3d9", "Direct3DCreate9", "PASS", "ok");

    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = WIN_W;
    pp.BackBufferHeight = WIN_H;
    pp.hDeviceWindow = hwnd;
    pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr))
        hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, hwnd,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr) || !dev)
    {
        report("d3d9", "CreateDevice", "FAIL", "hr=0x%08lx", hr);
        IDirect3D9_Release(d3d);
        return;
    }
    report("d3d9", "CreateDevice", "PASS", "ok");

    d3d9_draw_and_readback("d3d9", dev);

    /* Reset / device state. */
    hr = IDirect3DDevice9_TestCooperativeLevel(dev);
    check("d3d9", "TestCooperativeLevel", SUCCEEDED(hr), "hr=0x%08lx", hr);
    hr = IDirect3DDevice9_Reset(dev, &pp);
    check("d3d9", "Reset", SUCCEEDED(hr), "hr=0x%08lx", hr);
    if (SUCCEEDED(hr))
        d3d9_draw_and_readback("d3d9-after-reset", dev);

    IDirect3DDevice9_Release(dev);
    IDirect3D9_Release(d3d);
}

/* 9Ex: the common gaps item 5 targets (vblank, residency, GPU priority,
 * device state, shared resources). Judged against the documented success contract. */
static void run_d3d9ex(HWND hwnd, HMODULE mod)
{
    pDirect3DCreate9Ex create;
    IDirect3D9Ex *d3d = NULL;
    IDirect3DDevice9Ex *dev = NULL;
    IDirect3DDevice9Ex *dev2 = NULL;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;

    create = (pDirect3DCreate9Ex)GetProcAddress(mod, "Direct3DCreate9Ex");
    if (!create) { report("d3d9ex", "create-fn", "SKIP", "no Direct3DCreate9Ex export"); return; }
    hr = create(D3D_SDK_VERSION, &d3d);
    if (FAILED(hr) || !d3d) { report("d3d9ex", "Direct3DCreate9Ex", "FAIL", "hr=0x%08lx", hr); return; }
    report("d3d9ex", "Direct3DCreate9Ex", "PASS", "ok");

    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = WIN_W;
    pp.BackBufferHeight = WIN_H;
    pp.hDeviceWindow = hwnd;
    pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    hr = IDirect3D9Ex_CreateDeviceEx(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, NULL, &dev);
    if (FAILED(hr) || !dev)
    {
        report("d3d9ex", "CreateDeviceEx", "FAIL", "hr=0x%08lx", hr);
        IDirect3D9Ex_Release(d3d);
        return;
    }
    report("d3d9ex", "CreateDeviceEx", "PASS", "ok");

    d3d9_draw_and_readback("d3d9ex", (IDirect3DDevice9 *)dev);

    /* H10 WaitForVBlank: documented D3D_OK after waiting. */
    hr = IDirect3DDevice9Ex_WaitForVBlank(dev, 0);
    check("d3d9ex", "WaitForVBlank", hr == D3D_OK, "hr=0x%08lx", hr);

    /* H12 Set/GetGPUThreadPriority: D3D_OK and the value must round-trip. */
    hr = IDirect3DDevice9Ex_SetGPUThreadPriority(dev, 3);
    if (hr == D3D_OK)
    {
        INT prio = -999;
        HRESULT hr2 = IDirect3DDevice9Ex_GetGPUThreadPriority(dev, &prio);
        check("d3d9ex", "GPUThreadPriority", hr2 == D3D_OK && prio == 3,
              "set=D3D_OK get hr=0x%08lx prio=%d", hr2, prio);
    }
    else check("d3d9ex", "GPUThreadPriority", FALSE, "SetGPUThreadPriority hr=0x%08lx", hr);

    /* H11 CheckResourceResidency: a resource just created in DEFAULT is resident. */
    {
        IDirect3DTexture9 *tex = NULL;
        HANDLE shared = NULL;
        hr = IDirect3DDevice9Ex_CreateTexture(dev, 64, 64, 1, D3DUSAGE_RENDERTARGET,
                                              D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, &shared);
        if (FAILED(hr) || !tex)
        {
            report("d3d9ex", "CheckResourceResidency", "SKIP", "CreateTexture hr=0x%08lx", hr);
            report("d3d9ex", "shared-resource", "SKIP", "CreateTexture hr=0x%08lx", hr);
        }
        else
        {
            IDirect3DResource9 *res = (IDirect3DResource9 *)tex;
            hr = IDirect3DDevice9Ex_CheckResourceResidency(dev, &res, 1);
            check("d3d9ex", "CheckResourceResidency",
                  hr == S_OK || hr == S_RESIDENT_IN_SHARED_MEMORY,
                  "hr=0x%08lx", hr);

            /* resource sharing: open the shared handle on a second Ex device. */
            if (shared)
            {
                hr = IDirect3D9Ex_CreateDeviceEx(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, NULL, &dev2);
                if (SUCCEEDED(hr) && dev2)
                {
                    IDirect3DTexture9 *tex2 = NULL;
                    hr = IDirect3DDevice9Ex_CreateTexture(dev2, 64, 64, 1, D3DUSAGE_RENDERTARGET,
                                                          D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex2, &shared);
                    check("d3d9ex", "shared-resource", SUCCEEDED(hr) && tex2, "open hr=0x%08lx", hr);
                    if (tex2) IDirect3DTexture9_Release(tex2);
                }
                else report("d3d9ex", "shared-resource", "SKIP", "second device hr=0x%08lx", hr);
            }
            else report("d3d9ex", "shared-resource", "SKIP", "no shared handle produced");
            IDirect3DTexture9_Release(tex);
        }
    }

    /* device loss / recovery path. */
    hr = IDirect3DDevice9Ex_CheckDeviceState(dev, hwnd);
    check("d3d9ex", "CheckDeviceState", hr == D3D_OK || hr == S_PRESENT_OCCLUDED, "hr=0x%08lx", hr);

    /* ComposeRects: copy an 8x8 block of a known colour from src to (4,4) in dst. */
    {
        IDirect3DSurface9 *csrc = NULL, *cdst = NULL;
        IDirect3DVertexBuffer9 *sd = NULL, *dd = NULL;
        D3DLOCKED_RECT lr;
        void *p = NULL;
        struct { unsigned short x, y, w, h; } srcd = {0, 0, 8, 8};
        struct { unsigned short x, y; } dstd = {4, 4};
        int ok = 1, y;

        if (SUCCEEDED(IDirect3DDevice9Ex_CreateOffscreenPlainSurface(dev, 16, 16, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &csrc, NULL))
                && SUCCEEDED(IDirect3DDevice9Ex_CreateOffscreenPlainSurface(dev, 16, 16, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &cdst, NULL))
                && SUCCEEDED(IDirect3DDevice9Ex_CreateVertexBuffer(dev, sizeof(srcd), 0, 0, D3DPOOL_SYSTEMMEM, &sd, NULL))
                && SUCCEEDED(IDirect3DDevice9Ex_CreateVertexBuffer(dev, sizeof(dstd), 0, 0, D3DPOOL_SYSTEMMEM, &dd, NULL)))
        {
            IDirect3DSurface9_LockRect(csrc, &lr, NULL, 0);
            for (y = 0; y < 16; ++y) { int x; for (x = 0; x < 16; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xaabbccddu; }
            IDirect3DSurface9_UnlockRect(csrc);
            IDirect3DSurface9_LockRect(cdst, &lr, NULL, 0);
            for (y = 0; y < 16; ++y) { int x; for (x = 0; x < 16; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0; }
            IDirect3DSurface9_UnlockRect(cdst);
            if (SUCCEEDED(IDirect3DVertexBuffer9_Lock(sd, 0, 0, &p, 0))) { memcpy(p, &srcd, sizeof(srcd)); IDirect3DVertexBuffer9_Unlock(sd); }
            if (SUCCEEDED(IDirect3DVertexBuffer9_Lock(dd, 0, 0, &p, 0))) { memcpy(p, &dstd, sizeof(dstd)); IDirect3DVertexBuffer9_Unlock(dd); }

            hr = IDirect3DDevice9Ex_ComposeRects(dev, csrc, cdst, sd, 1, dd, D3DCOMPOSERECTS_COPY, 0, 0);
            if (SUCCEEDED(hr))
            {
                IDirect3DSurface9_LockRect(cdst, &lr, NULL, D3DLOCK_READONLY);
                if ((((DWORD *)((BYTE *)lr.pBits + 4 * lr.Pitch))[4]) != 0xaabbccddu) ok = 0; /* copied */
                if ((((DWORD *)((BYTE *)lr.pBits + 0 * lr.Pitch))[0]) != 0u) ok = 0;            /* outside untouched */
                IDirect3DSurface9_UnlockRect(cdst);
                check("d3d9ex", "ComposeRects", ok, "copy 8x8 to (4,4): inside/outside %s", ok ? "ok" : "wrong");
            }
            else report("d3d9ex", "ComposeRects", "FAIL", "hr=0x%08lx", hr);
        }
        else report("d3d9ex", "ComposeRects", "SKIP", "surface/VB creation failed");
        if (sd) IDirect3DVertexBuffer9_Release(sd);
        if (dd) IDirect3DVertexBuffer9_Release(dd);
        if (csrc) IDirect3DSurface9_Release(csrc);
        if (cdst) IDirect3DSurface9_Release(cdst);
    }

    if (dev2) IDirect3DDevice9Ex_Release(dev2);
    IDirect3DDevice9Ex_Release(dev);
    IDirect3D9Ex_Release(d3d);
}

static void run_ddraw(HWND hwnd, HMODULE mod)
{
    typedef HRESULT (WINAPI *pDDCreateEx)(GUID *, LPVOID *, REFIID, IUnknown *);
    pDDCreateEx create;
    IDirectDraw7 *dd = NULL;
    IDirectDrawSurface7 *surf = NULL;
    DDSURFACEDESC2 desc;
    DDBLTFX fx;
    HRESULT hr;

    create = (pDDCreateEx)GetProcAddress(mod, "DirectDrawCreateEx");
    if (!create) { report("ddraw", "create-fn", "FAIL", "no DirectDrawCreateEx export"); return; }
    hr = create(NULL, (LPVOID *)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(hr) || !dd) { report("ddraw", "DirectDrawCreateEx", "FAIL", "hr=0x%08lx", hr); return; }
    report("ddraw", "DirectDrawCreateEx", "PASS", "ok");

    hr = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    check("ddraw", "SetCooperativeLevel", SUCCEEDED(hr), "hr=0x%08lx", hr);

    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = WIN_W;
    desc.dwHeight = WIN_H;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
    desc.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
    desc.ddpfPixelFormat.dwBBitMask = 0x000000ff;
    hr = IDirectDraw7_CreateSurface(dd, &desc, &surf, NULL);
    if (FAILED(hr) || !surf)
    {
        report("ddraw", "CreateSurface", "FAIL", "hr=0x%08lx", hr);
        IDirectDraw7_Release(dd);
        return;
    }
    report("ddraw", "CreateSurface", "PASS", "ok");

    ZeroMemory(&fx, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = 0x00336699u;
    hr = IDirectDrawSurface7_Blt(surf, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    check("ddraw", "Blt-colorfill", SUCCEEDED(hr), "hr=0x%08lx", hr);

    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface7_Lock(surf, NULL, &desc, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    if (FAILED(hr))
    {
        report("ddraw", "readback", "SKIP", "Lock hr=0x%08lx", hr);
    }
    else
    {
        DWORD px = *(DWORD *)((BYTE *)desc.lpSurface + 128 * desc.lPitch + 128 * 4) & 0x00ffffff;
        IDirectDrawSurface7_Unlock(surf, NULL);
        check("ddraw", "readback-colorfill", argb_close(px, 0x00336699u, 2),
              "px=0x%06lx want 0x336699", px);
    }

    /* H09 WaitForVerticalBlank: documented DD_OK (real wait) or a documented unsupported. */
    hr = IDirectDraw7_WaitForVerticalBlank(dd, DDWAITVB_BLOCKBEGIN, NULL);
    check("ddraw", "WaitForVerticalBlank", hr == DD_OK, "hr=0x%08lx", hr);
    {
        BOOL invb = FALSE;
        hr = IDirectDraw7_GetVerticalBlankStatus(dd, &invb);
        check("ddraw", "GetVerticalBlankStatus", SUCCEEDED(hr), "hr=0x%08lx inVB=%d", hr, invb);
    }

    IDirectDrawSurface7_Release(surf);
    IDirectDraw7_Release(dd);
}

int main(void)
{
    HWND hwnd;
    HMODULE d3d9mod, ddrawmod;

    hwnd = make_window();
    if (!hwnd) { fprintf(stderr, "no window\n"); return 99; }
    pump();

    d3d9mod = LoadLibraryW(L"d3d9.dll");
    if (d3d9mod) { run_d3d9(hwnd, d3d9mod); run_d3d9ex(hwnd, d3d9mod); }
    else report("d3d9", "LoadLibrary", "FAIL", "d3d9.dll not found");

    run_d3d8(hwnd);

    ddrawmod = LoadLibraryW(L"ddraw.dll");
    if (ddrawmod) run_ddraw(hwnd, ddrawmod);
    else report("ddraw", "LoadLibrary", "FAIL", "ddraw.dll not found");

    printf("LEGACY v%d SUMMARY pass=%d fail=%d skip=%d\n",
           LEGACY_VERSION, pass_count, fail_count, skip_count);
    fflush(stdout);
    Sleep(1500);
    return fail_count;
}
