/*
 * LiberX contract fixture: IDWriteFontDownloadQueue (no remote fonts).
 *
 * With no remote fonts the queue is always empty: IsEmpty = TRUE,
 * GetGenerationCount = 0, BeginDownload/CancelDownload = S_OK, and
 * Add/RemoveListener accept and return S_OK (the listener stays inert).
 * Previously Add/Remove/Begin/Cancel were E_NOTIMPL. SKIPs if unavailable.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <dwrite_3.h>

static int fail_count, skip_count, pass_count;
static void rep(const char *n,const char *st,const char *f,...){char d[200];va_list a;va_start(a,f);vsnprintf(d,sizeof(d),f,a);va_end(a);printf("DWRITE_DLQUEUE v1 %s %s %s\n",n,st,d);if(st[0]=='F')++fail_count;else if(st[0]=='S')++skip_count;else ++pass_count;}
static void chk(const char *n,BOOL ok,const char *f,...){char d[200];va_list a;va_start(a,f);vsnprintf(d,sizeof(d),f,a);va_end(a);rep(n,ok?"PASS":"FAIL","%s",d);}

int main(void)
{
    IDWriteFactory3 *factory=NULL; IDWriteFontDownloadQueue *q=NULL;
    HRESULT hr; UINT32 token=0;

    hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,&IID_IDWriteFactory3,(IUnknown**)&factory);
    if(FAILED(hr)||!factory){ rep("create_factory3","SKIP","hr=0x%08lx",hr); goto done; }
    hr=IDWriteFactory3_GetFontDownloadQueue(factory,&q);
    if(FAILED(hr)||!q){ rep("get_queue","SKIP","hr=0x%08lx",hr); goto done; }

    chk("is_empty",IDWriteFontDownloadQueue_IsEmpty(q)==TRUE,"IsEmpty");
    chk("generation",IDWriteFontDownloadQueue_GetGenerationCount(q)==0,"GetGenerationCount");
    hr=IDWriteFontDownloadQueue_BeginDownload(q,NULL);
    chk("begin_download",SUCCEEDED(hr),"BeginDownload hr=0x%08lx (not E_NOTIMPL)",hr);
    hr=IDWriteFontDownloadQueue_CancelDownload(q);
    chk("cancel_download",SUCCEEDED(hr),"CancelDownload hr=0x%08lx",hr);
    hr=IDWriteFontDownloadQueue_AddListener(q,NULL,&token);
    chk("add_listener_null",hr==E_INVALIDARG,"AddListener(NULL) hr=0x%08lx",hr);

done:
    if(q)IDWriteFontDownloadQueue_Release(q);
    if(factory)IDWriteFactory3_Release(factory);
    printf("DWRITE_DLQUEUE v1 SUMMARY pass=%d fail=%d skip=%d\n",pass_count,fail_count,skip_count);
    return fail_count;
}
