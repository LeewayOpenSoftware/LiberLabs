/*
 * LiberX contract fixture: ID3D11Device::GetCreationFlags.
 *
 * GetCreationFlags was a stub returning 0. The flags passed to D3D11CreateDevice
 * are now captured in D3D11CoreCreateDevice and returned verbatim (Windows
 * behaviour). This fixture creates a device with D3D11_CREATE_DEVICE_BGRA_SUPPORT
 * and checks GetCreationFlags returns exactly that. SKIPs when no device.
 */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <d3d11.h>

static int fail_count, skip_count, pass_count;
static void rep(const char *n,const char *st,const char *d){printf("D3D11_CREATIONFLAGS v1 %s %s %s\n",n,st,d);if(st[0]=='F')++fail_count;else if(st[0]=='S')++skip_count;else ++pass_count;}

int main(void)
{
    ID3D11Device *dev=NULL; HRESULT hr; UINT got; char d[128];
    static const D3D_FEATURE_LEVEL fl[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
    const UINT want=D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,want,fl,2,D3D11_SDK_VERSION,&dev,NULL,NULL);
    if(FAILED(hr)||!dev) hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,want,fl,2,D3D11_SDK_VERSION,&dev,NULL,NULL);
    if(FAILED(hr)||!dev){ sprintf(d,"D3D11CreateDevice hr=0x%08lx",hr); rep("create_device","SKIP",d); goto done; }

    got=ID3D11Device_GetCreationFlags(dev);
    sprintf(d,"want 0x%08x got 0x%08x",want,got);
    rep("creationflags_roundtrip", got==want ? "PASS":"FAIL", d);

    ID3D11Device_Release(dev);
done:
    printf("D3D11_CREATIONFLAGS v1 SUMMARY pass=%d fail=%d skip=%d\n",pass_count,fail_count,skip_count);
    return fail_count;
}
