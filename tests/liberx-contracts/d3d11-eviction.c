/*
 * LiberX contract fixture: ID3D11Resource eviction-priority round-trip.
 *
 * d3d11/d3d10 Set/GetEvictionPriority on buffers and textures were stubs
 * (Set = no-op, Get = 0). The documented contract is that the value set is the
 * value returned. This fixture creates a D3D11 device, a buffer and a texture,
 * and checks the round-trip. SKIPs (not PASS) when no device can be created.
 */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <d3d11.h>

#define TEST_VERSION 1
static int fail_count, skip_count, pass_count;
static void report(const char *n,const char *st,const char *f,...){char d[256];va_list a;va_start(a,f);vsnprintf(d,sizeof(d),f,a);va_end(a);printf("D3D11_EVICTION v%d %s %s %s\n",TEST_VERSION,n,st,d);if(st[0]=='F')++fail_count;else if(st[0]=='S')++skip_count;else ++pass_count;}
static void check(const char *n,BOOL ok,const char *f,...){char d[256];va_list a;va_start(a,f);vsnprintf(d,sizeof(d),f,a);va_end(a);report(n,ok?"PASS":"FAIL","%s",d);}

int main(void)
{
    ID3D11Device *dev=NULL; ID3D11Buffer *buf=NULL; ID3D11Texture2D *tex=NULL;
    HRESULT hr; UINT p;
    static const D3D_FEATURE_LEVEL fl[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
    D3D11_BUFFER_DESC bd; D3D11_TEXTURE2D_DESC td;

    hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,fl,2,D3D11_SDK_VERSION,&dev,NULL,NULL);
    if(FAILED(hr)||!dev) hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,0,fl,2,D3D11_SDK_VERSION,&dev,NULL,NULL);
    if(FAILED(hr)||!dev){report("create_device","SKIP","hr=0x%08lx",hr);goto done;}

    memset(&bd,0,sizeof(bd)); bd.ByteWidth=1024; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    hr=ID3D11Device_CreateBuffer(dev,&bd,NULL,&buf);
    if(SUCCEEDED(hr)&&buf){
        ID3D11Buffer_SetEvictionPriority(buf,0x60000000u);
        p=ID3D11Buffer_GetEvictionPriority(buf);
        check("buffer_roundtrip",p==0x60000000u,"set 0x60000000 got 0x%08x",p);
        ID3D11Buffer_SetEvictionPriority(buf,0x28000000u);
        p=ID3D11Buffer_GetEvictionPriority(buf);
        check("buffer_roundtrip2",p==0x28000000u,"set 0x28000000 got 0x%08x",p);
    } else report("create_buffer","SKIP","hr=0x%08lx",hr);

    memset(&td,0,sizeof(td)); td.Width=16; td.Height=16; td.MipLevels=1; td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    hr=ID3D11Device_CreateTexture2D(dev,&td,NULL,&tex);
    if(SUCCEEDED(hr)&&tex){
        ID3D11Texture2D_SetEvictionPriority(tex,0x70000000u);
        p=ID3D11Texture2D_GetEvictionPriority(tex);
        check("texture2d_roundtrip",p==0x70000000u,"set 0x70000000 got 0x%08x",p);
    } else report("create_texture2d","SKIP","hr=0x%08lx",hr);

    /* exception-mode round-trip (same device) */
    {
        HRESULT ehr; UINT em;
        ehr=ID3D11Device_SetExceptionMode(dev,0);
        em=ID3D11Device_GetExceptionMode(dev);
        check("exception_mode_zero",SUCCEEDED(ehr)&&em==0,"Set(0) hr=0x%08lx get=%u",ehr,em);
        ehr=ID3D11Device_SetExceptionMode(dev,1);
        em=ID3D11Device_GetExceptionMode(dev);
        check("exception_mode_one",SUCCEEDED(ehr)&&em==1,"Set(1) hr=0x%08lx get=%u",ehr,em);
        ehr=ID3D11Device_SetExceptionMode(dev,2);
        check("exception_mode_bad",ehr==E_INVALIDARG,"Set(2) hr=0x%08lx (expect E_INVALIDARG)",ehr);
    }

done:
    if(tex) ID3D11Texture2D_Release(tex);
    if(buf) ID3D11Buffer_Release(buf);
    if(dev) ID3D11Device_Release(dev);
    printf("D3D11_EVICTION v%d SUMMARY pass=%d fail=%d skip=%d\n",TEST_VERSION,pass_count,fail_count,skip_count);
    return fail_count;
}
