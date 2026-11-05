/*
 * LiberX contract fixture: DirectWrite font/glyph locality.
 *
 * For locally installed fonts the honest result is "local": IDWriteFont3/
 * IDWriteFontFaceReference::GetLocality = DWRITE_LOCALITY_LOCAL, and
 * IDWriteFontFace3::IsCharacterLocal/IsGlyphLocal = TRUE,
 * AreCharactersLocal/AreGlyphsLocal set *are_local = TRUE. Previously these were
 * FALSE / E_NOTIMPL. SKIPs when the factory or a system font is unavailable.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <dwrite_3.h>

static int fail_count, skip_count, pass_count;
static void rep(const char *n,const char *st,const char *f,...){char d[200];va_list a;va_start(a,f);vsnprintf(d,sizeof(d),f,a);va_end(a);printf("DWRITE_LOCALITY v1 %s %s %s\n",n,st,d);if(st[0]=='F')++fail_count;else if(st[0]=='S')++skip_count;else ++pass_count;}
static void chk(const char *n,BOOL ok,const char *f,...){char d[200];va_list a;va_start(a,f);vsnprintf(d,sizeof(d),f,a);va_end(a);rep(n,ok?"PASS":"FAIL","%s",d);}

int main(void)
{
    IDWriteFactory *factory=NULL; IDWriteFontCollection *coll=NULL; IDWriteFontFamily *fam=NULL;
    IDWriteFont *font=NULL; IDWriteFont3 *font3=NULL; IDWriteFontFace *face=NULL; IDWriteFontFace3 *face3=NULL;
    HRESULT hr; UINT32 n; BOOL exists=FALSE, are_local=FALSE; static const WCHAR t[]={'A','B','C'};

    hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,&IID_IDWriteFactory,(IUnknown**)&factory);
    if(FAILED(hr)||!factory){ rep("create_factory","SKIP","hr=0x%08lx",hr); goto done; }
    hr=IDWriteFactory_GetSystemFontCollection(factory,&coll,FALSE);
    if(FAILED(hr)||!coll){ rep("system_collection","SKIP","hr=0x%08lx",hr); goto done; }
    n=IDWriteFontCollection_GetFontFamilyCount(coll);
    if(!n){ rep("font_family","SKIP","no families"); goto done; }
    hr=IDWriteFontCollection_GetFontFamily(coll,0,&fam);
    if(FAILED(hr)||!fam){ rep("get_family","SKIP","hr=0x%08lx",hr); goto done; }
    hr=IDWriteFontFamily_GetFont(fam,0,&font);
    if(FAILED(hr)||!font){ rep("get_font","SKIP","hr=0x%08lx",hr); goto done; }

    if(SUCCEEDED(IDWriteFont_QueryInterface(font,&IID_IDWriteFont3,(void**)&font3)) && font3)
        chk("font3_locality",IDWriteFont3_GetLocality(font3)==DWRITE_LOCALITY_LOCAL,"GetLocality");
    else rep("font3_qi","SKIP","no IDWriteFont3");

    hr=IDWriteFont_CreateFontFace(font,&face);
    if(SUCCEEDED(hr)&&face && SUCCEEDED(IDWriteFontFace_QueryInterface(face,&IID_IDWriteFontFace3,(void**)&face3)) && face3){
        chk("ischaracterlocal",IDWriteFontFace3_IsCharacterLocal(face3,'A')==TRUE,"IsCharacterLocal('A')");
        are_local=FALSE;
        hr=IDWriteFontFace3_AreCharactersLocal(face3,t,3,FALSE,&are_local);
        chk("arecharacterslocal",SUCCEEDED(hr)&&are_local==TRUE,"hr=0x%08lx are_local=%d",hr,are_local);
        hr=IDWriteFontFace3_AreCharactersLocal(face3,NULL,3,FALSE,&are_local);
        chk("arecharacterslocal_null",hr==E_INVALIDARG,"NULL text hr=0x%08lx",hr);
    } else rep("fontface3_qi","SKIP","no IDWriteFontFace3 (hr=0x%08lx)",hr);

done:
    if(face3)IDWriteFontFace3_Release(face3); if(face)IDWriteFontFace_Release(face);
    if(font3)IDWriteFont3_Release(font3); if(font)IDWriteFont_Release(font);
    if(fam)IDWriteFontFamily_Release(fam); if(coll)IDWriteFontCollection_Release(coll);
    if(factory)IDWriteFactory_Release(factory);
    (void)exists;
    printf("DWRITE_LOCALITY v1 SUMMARY pass=%d fail=%d skip=%d\n",pass_count,fail_count,skip_count);
    return fail_count;
}
