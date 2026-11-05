/*
 * LiberX contract fixture: d3d10core!D3D10CoreRegisterLayers.
 *
 * It was E_NOTIMPL; it now delegates to d3d11's D3D11CoreRegisterLayers (the
 * shared D3D10/D3D11 device layer) and returns S_OK when d3d11 is present, or an
 * honest DXGI_ERROR_UNSUPPORTED when it is not. Deterministic (no device).
 */
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *reg_func)(void);

static int fail_count, skip_count, pass_count;
static void rep(const char *n,const char *st,const char *d){printf("D3D10CORE_LAYERS v1 %s %s %s\n",n,st,d);if(st[0]=='F')++fail_count;else if(st[0]=='S')++skip_count;else ++pass_count;}

int main(void)
{
    HMODULE h=LoadLibraryA("d3d10core.dll");
    reg_func reg; HRESULT hr; char d[128];
    if(!h){ rep("load","SKIP","d3d10core.dll not found"); goto done; }
    reg=(reg_func)GetProcAddress(h,"D3D10CoreRegisterLayers");
    if(!reg){ rep("export","FAIL","D3D10CoreRegisterLayers not found"); FreeLibrary(h); goto done; }
    hr=reg();
    sprintf(d,"hr=0x%08lx (expect S_OK with d3d11 present, else DXGI_ERROR_UNSUPPORTED 0x887a0004)",hr);
    /* not E_NOTIMPL (0x80004001) any more: either success or the honest unsupported code */
    rep("register_layers", (hr==S_OK || hr==(HRESULT)0x887a0004) ? "PASS":"FAIL", d);
    FreeLibrary(h);
done:
    printf("D3D10CORE_LAYERS v1 SUMMARY pass=%d fail=%d skip=%d\n",pass_count,fail_count,skip_count);
    return fail_count;
}
