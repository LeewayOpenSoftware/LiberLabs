/*
 * LiberX item 5: D3D8 end-to-end render (clear + draw + lockable-back-buffer readback).
 * Separate translation unit so d3d8.h does not clash with d3d9.h in legacy_render.c.
 */
#define COBJMACROS
#define CONST_VTABLE
#include <windows.h>
#include <d3d8.h>
#include "legacy.h"

#define WIN_W 256
#define WIN_H 256
#define CLEAR_RGB 0x00204080u
#define QUAD_RGB  0x00a01030u

typedef IDirect3D8 *(WINAPI *pDirect3DCreate8)(UINT);

struct vtx8 { float x, y, z, rhw; DWORD color; };

static BOOL argb_close8(DWORD got, DWORD exp, int tol)
{
    int i;
    for (i = 0; i < 3; ++i)
    {
        int d = ((got >> (i * 8)) & 0xff) - ((exp >> (i * 8)) & 0xff);
        if (d < 0) d = -d;
        if (d > tol) return FALSE;
    }
    return TRUE;
}

void run_d3d8(HWND hwnd)
{
    HMODULE mod;
    pDirect3DCreate8 create;
    IDirect3D8 *d3d;
    IDirect3DDevice8 *dev = NULL;
    IDirect3DSurface8 *bb = NULL;
    D3DPRESENT_PARAMETERS pp;
    D3DLOCKED_RECT lr;
    HRESULT hr;
    struct vtx8 quad[4] =
    {
        {  64.0f,  64.0f, 0.5f, 1.0f, QUAD_RGB },
        { 192.0f,  64.0f, 0.5f, 1.0f, QUAD_RGB },
        {  64.0f, 192.0f, 0.5f, 1.0f, QUAD_RGB },
        { 192.0f, 192.0f, 0.5f, 1.0f, QUAD_RGB },
    };

    mod = LoadLibraryW(L"d3d8.dll");
    if (!mod) { report("d3d8", "LoadLibrary", "FAIL", "d3d8.dll not found"); return; }
    create = (pDirect3DCreate8)GetProcAddress(mod, "Direct3DCreate8");
    if (!create) { report("d3d8", "create-fn", "FAIL", "no Direct3DCreate8 export"); return; }
    d3d = create(D3D_SDK_VERSION);
    if (!d3d) { report("d3d8", "Direct3DCreate8", "FAIL", "returned NULL"); return; }
    report("d3d8", "Direct3DCreate8", "PASS", "ok");

    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = WIN_W;
    pp.BackBufferHeight = WIN_H;
    pp.hDeviceWindow = hwnd;
    pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    hr = IDirect3D8_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr))
        hr = IDirect3D8_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, hwnd,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr) || !dev)
    {
        report("d3d8", "CreateDevice", "FAIL", "hr=0x%08lx", hr);
        IDirect3D8_Release(d3d);
        return;
    }
    report("d3d8", "CreateDevice", "PASS", "ok");

    hr = IDirect3DDevice8_Clear(dev, 0, NULL, D3DCLEAR_TARGET, CLEAR_RGB, 1.0f, 0);
    check("d3d8", "clear", SUCCEEDED(hr), "hr=0x%08lx", hr);

    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetVertexShader(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    hr = IDirect3DDevice8_BeginScene(dev);
    if (SUCCEEDED(hr))
    {
        hr = IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
        IDirect3DDevice8_EndScene(dev);
    }
    check("d3d8", "draw-quad", SUCCEEDED(hr), "hr=0x%08lx", hr);

    hr = IDirect3DDevice8_GetBackBuffer(dev, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if (FAILED(hr) || !bb)
    {
        report("d3d8", "readback", "SKIP", "GetBackBuffer hr=0x%08lx", hr);
    }
    else
    {
        hr = IDirect3DSurface8_LockRect(bb, &lr, NULL, D3DLOCK_READONLY);
        if (FAILED(hr))
        {
            report("d3d8", "readback", "SKIP", "LockRect hr=0x%08lx", hr);
        }
        else
        {
            DWORD border = *(DWORD *)((BYTE *)lr.pBits + 16 * lr.Pitch + 16 * 4) & 0x00ffffff;
            DWORD centre = *(DWORD *)((BYTE *)lr.pBits + 128 * lr.Pitch + 128 * 4) & 0x00ffffff;
            IDirect3DSurface8_UnlockRect(bb);
            check("d3d8", "readback-clear", argb_close8(border, CLEAR_RGB, 2),
                  "border=0x%06lx want 0x%06lx", border, CLEAR_RGB);
            check("d3d8", "readback-draw", argb_close8(centre, QUAD_RGB, 2),
                  "centre=0x%06lx want 0x%06lx", centre, QUAD_RGB);
        }
        IDirect3DSurface8_Release(bb);
    }

    hr = IDirect3DDevice8_Present(dev, NULL, NULL, NULL, NULL);
    check("d3d8", "present", SUCCEEDED(hr), "hr=0x%08lx", hr);

    hr = IDirect3DDevice8_TestCooperativeLevel(dev);
    check("d3d8", "TestCooperativeLevel", SUCCEEDED(hr), "hr=0x%08lx", hr);
    hr = IDirect3DDevice8_Reset(dev, &pp);
    check("d3d8", "Reset", SUCCEEDED(hr), "hr=0x%08lx", hr);

    IDirect3DDevice8_Release(dev);
    IDirect3D8_Release(d3d);
}
