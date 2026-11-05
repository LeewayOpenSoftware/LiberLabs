/*
 * LiberX contract fixture: D3D10Get{Vertex,Pixel,Geometry}ShaderProfile.
 *
 * These return the Shader Model 4.0 profile strings for Direct3D 10. They were
 * logged as stubs but already returned the correct values; this fixture pins the
 * contract (vs_4_0 / ps_4_0 / gs_4_0). No device needed - deterministic.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef const char * (WINAPI *profile_func)(void *);

static int fail_count, skip_count, pass_count;
static void check(const char *n,int ok,const char *got){printf("D3D10_PROFILE v1 %s %s %s\n",n,ok?"PASS":"FAIL",got?got:"(null)");if(ok)++pass_count;else ++fail_count;}

int main(void)
{
    HMODULE h=LoadLibraryA("d3d10.dll");
    profile_func vs,ps,gs; const char *r;
    if(!h){printf("D3D10_PROFILE v1 load SKIP d3d10.dll not found\n");++skip_count;goto done;}
    vs=(profile_func)GetProcAddress(h,"D3D10GetVertexShaderProfile");
    ps=(profile_func)GetProcAddress(h,"D3D10GetPixelShaderProfile");
    gs=(profile_func)GetProcAddress(h,"D3D10GetGeometryShaderProfile");
    if(!vs||!ps||!gs){printf("D3D10_PROFILE v1 exports FAIL vs=%p ps=%p gs=%p\n",(void*)vs,(void*)ps,(void*)gs);++fail_count;FreeLibrary(h);goto done;}
    r=vs(NULL); check("vertex", r&&!strcmp(r,"vs_4_0"), r);
    r=ps(NULL); check("pixel",  r&&!strcmp(r,"ps_4_0"), r);
    r=gs(NULL); check("geometry",r&&!strcmp(r,"gs_4_0"), r);
    FreeLibrary(h);
done:
    printf("D3D10_PROFILE v1 SUMMARY pass=%d fail=%d skip=%d\n",pass_count,fail_count,skip_count);
    return fail_count;
}
