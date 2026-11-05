/*
 * LiberX contract fixture: DXGI residency / offer / reclaim + factory IsCurrent.
 *
 * IDXGIDevice QueryResourceResidency/OfferResources/ReclaimResources were
 * E_NOTIMPL stubs. The software path keeps every resource resident and never
 * discards, so the documented honest results are FULLY_RESIDENT / accepted /
 * not-discarded. IDXGIFactory::IsCurrent returns TRUE on a static adapter set.
 * IsCurrent needs only a factory (no GPU) and always runs; the residency checks
 * SKIP when no D3D11 device can be created. Never asserts fake success.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#define TEST_VERSION 1
static int fail_count, skip_count, pass_count;
static void report(const char *n,const char *st,const char *f,...){char d[256];va_list a;va_start(a,f);vsnprintf(d,sizeof(d),f,a);va_end(a);printf("DXGI_RESIDENCY v%d %s %s %s\n",TEST_VERSION,n,st,d);if(st[0]=='F')++fail_count;else if(st[0]=='S')++skip_count;else ++pass_count;}
static void check(const char *n,BOOL ok,const char *f,...){char d[256];va_list a;va_start(a,f);vsnprintf(d,sizeof(d),f,a);va_end(a);report(n,ok?"PASS":"FAIL","%s",d);}

int main(void)
{
    IDXGIFactory1 *factory=NULL;
    ID3D11Device *dev=NULL; IDXGIDevice *dxgi=NULL; ID3D11Buffer *buf=NULL; IDXGIResource *res=NULL;
    HRESULT hr; DXGI_RESIDENCY residency; BOOL discarded; IUnknown *resarr[1];
    static const D3D_FEATURE_LEVEL fl[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
    D3D11_BUFFER_DESC bd;

    /* IsCurrent: factory only, deterministic */
    hr=CreateDXGIFactory1(&IID_IDXGIFactory1,(void**)&factory);
    if(SUCCEEDED(hr)&&factory){
        check("factory_iscurrent",IDXGIFactory1_IsCurrent(factory)==TRUE,"IsCurrent must be TRUE on a static adapter set");
    } else report("create_factory","SKIP","hr=0x%08lx",hr);

    hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,fl,2,D3D11_SDK_VERSION,&dev,NULL,NULL);
    if(FAILED(hr)||!dev) hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,0,fl,2,D3D11_SDK_VERSION,&dev,NULL,NULL);
    if(FAILED(hr)||!dev){report("create_device","SKIP","hr=0x%08lx",hr);goto done;}

    hr=ID3D11Device_QueryInterface(dev,&IID_IDXGIDevice,(void**)&dxgi);
    if(FAILED(hr)||!dxgi){report("qi_dxgidevice","SKIP","hr=0x%08lx",hr);goto done;}

    memset(&bd,0,sizeof(bd)); bd.ByteWidth=1024; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    hr=ID3D11Device_CreateBuffer(dev,&bd,NULL,&buf);
    if(FAILED(hr)||!buf){report("create_buffer","SKIP","hr=0x%08lx",hr);goto done;}
    hr=ID3D11Buffer_QueryInterface(buf,&IID_IDXGIResource,(void**)&res);
    if(FAILED(hr)||!res){report("qi_dxgiresource","SKIP","hr=0x%08lx",hr);goto done;}

    resarr[0]=(IUnknown*)res;
    residency=(DXGI_RESIDENCY)0;
    hr=IDXGIDevice_QueryResourceResidency(dxgi,resarr,&residency,1);
    check("residency",SUCCEEDED(hr)&&residency==DXGI_RESIDENCY_FULLY_RESIDENT,"hr=0x%08lx residency=%d (expect FULLY_RESIDENT=1)",hr,residency);

    hr=IDXGIDevice_QueryResourceResidency(dxgi,NULL,&residency,1);
    check("residency_null",hr==E_INVALIDARG,"NULL resources hr=0x%08lx (expect E_INVALIDARG)",hr);

    {
        IDXGIDevice2 *dev2=NULL;
        hr=IDXGIDevice_QueryInterface(dxgi,&IID_IDXGIDevice2,(void**)&dev2);
        if(SUCCEEDED(hr)&&dev2){
            IDXGIResource *offer[1]; offer[0]=res;
            hr=IDXGIDevice2_OfferResources(dev2,1,offer,DXGI_OFFER_RESOURCE_PRIORITY_NORMAL);
            check("offer",SUCCEEDED(hr),"OfferResources hr=0x%08lx",hr);
            hr=IDXGIDevice2_OfferResources(dev2,1,offer,(DXGI_OFFER_RESOURCE_PRIORITY)999);
            check("offer_bad_priority",hr==E_INVALIDARG,"bad priority hr=0x%08lx (expect E_INVALIDARG)",hr);
            discarded=TRUE;
            hr=IDXGIDevice2_ReclaimResources(dev2,1,offer,&discarded);
            check("reclaim",SUCCEEDED(hr)&&discarded==FALSE,"Reclaim hr=0x%08lx discarded=%d (expect 0)",hr,discarded);
            IDXGIDevice2_Release(dev2);
        } else report("qi_dxgidevice2","SKIP","hr=0x%08lx",hr);
    }

done:
    if(res) IDXGIResource_Release(res);
    if(buf) ID3D11Buffer_Release(buf);
    if(dxgi) IDXGIDevice_Release(dxgi);
    if(dev) ID3D11Device_Release(dev);
    if(factory) IDXGIFactory1_Release(factory);
    printf("DXGI_RESIDENCY v%d SUMMARY pass=%d fail=%d skip=%d\n",TEST_VERSION,pass_count,fail_count,skip_count);
    return fail_count;
}
