/*
 * PROJECT:     LiberX DXC Level 1 - differential test suite (CPU-only)
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Exercise dxcompiler.dll's genuinely-implemented Level 1 surface
 *              with known inputs and expected properties: HLSL->SM<=5 compile
 *              (DXBC), the defined SM6 refusal, DXBC/DXIL disassembly, blob
 *              round-trips, and validator well-formedness. Loads the DLL at
 *              runtime (DxcCreateInstance); no GPU, no device.
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *                <281492504+LeewayOpenSoftware@users.noreply.github.com>
 */

#include <windows.h>
#include <unknwn.h>
#include <dxcapi.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>

/* Local IIDs (avoid any __uuidof dependency). */
static const IID LX_IID_IDxcCompiler3 =
    {0x228B4687,0x5A6A,0x4730,{0x90,0x0C,0x97,0x02,0xB2,0x20,0x3F,0x54}};
static const IID LX_IID_IDxcUtils =
    {0x4605C4CB,0x2019,0x492A,{0xAD,0xA4,0x65,0xF2,0x0B,0xB7,0xD6,0x7F}};
static const IID LX_IID_IDxcValidator =
    {0xA6E82BD2,0x1FD7,0x4826,{0x98,0x11,0x28,0x57,0xE7,0x97,0xF4,0x9A}};
static const IID LX_IID_IDxcResult =
    {0x58346CDA,0xDDE7,0x4497,{0x94,0x61,0x6F,0x87,0xAF,0x5E,0x06,0x59}};

/* Must match dxcompiler.cpp. */
#define LX_DXC_E_SM6_UNSUPPORTED ((HRESULT)0x80AA0006L)

static int g_pass, g_fail, g_skip;

static void check(const char *name, bool ok, const char *fmt, ...)
{
    char detail[256];
    va_list a; va_start(a, fmt); vsnprintf(detail, sizeof detail, fmt, a); va_end(a);
    printf("DXC-DIFF %s %s %s\n", name, ok ? "PASS" : "FAIL", detail);
    if (ok) ++g_pass; else ++g_fail;
}
static void skip(const char *name, const char *why)
{
    printf("DXC-DIFF %s SKIP %s\n", name, why);
    ++g_skip;
}

typedef HRESULT (__stdcall *DxcCreateInstanceProc)(REFCLSID, REFIID, LPVOID *);

static const char *g_hlsl_ps =
    "float4 main() : SV_Target { return float4(1,0,0,1); }";

int main(void)
{
    HMODULE h = LoadLibraryW(L"dxcompiler.dll");
    if (!h) { printf("DXC-DIFF load FAIL could not LoadLibrary dxcompiler.dll\n");
              printf("DXC-DIFF-RESULT PASS=0 FAIL=1 SKIP=0\n"); return 1; }
    DxcCreateInstanceProc create =
        (DxcCreateInstanceProc)GetProcAddress(h, "DxcCreateInstance");
    check("entry.DxcCreateInstance", create != nullptr, "GetProcAddress");
    if (!create) { printf("DXC-DIFF-RESULT PASS=%d FAIL=%d SKIP=%d\n", g_pass, g_fail, g_skip); return 1; }

    /* 1. create-instance + QI contract */
    IDxcCompiler3 *comp = nullptr;
    HRESULT hr = create(CLSID_DxcCompiler, LX_IID_IDxcCompiler3, (void **)&comp);
    check("create.IDxcCompiler3", SUCCEEDED(hr) && comp, "hr=0x%08lX", (unsigned long)hr);
    IDxcUtils *utils = nullptr;
    hr = create(CLSID_DxcUtils, LX_IID_IDxcUtils, (void **)&utils);
    check("create.IDxcUtils", SUCCEEDED(hr) && utils, "hr=0x%08lX", (unsigned long)hr);
    IDxcValidator *val = nullptr;
    hr = create(CLSID_DxcValidator, LX_IID_IDxcValidator, (void **)&val);
    check("create.IDxcValidator", SUCCEEDED(hr) && val, "hr=0x%08lX", (unsigned long)hr);

    DxcBuffer src; src.Ptr = g_hlsl_ps; src.Size = strlen(g_hlsl_ps); src.Encoding = DXC_CP_UTF8;

    /* 3. compile HLSL -> SM5 (DXBC) */
    IDxcBlob *dxbc = nullptr;
    if (comp)
    {
        LPCWSTR args5[] = { L"-T", L"ps_5_0", L"-E", L"main" };
        IDxcResult *res = nullptr;
        hr = comp->Compile(&src, args5, 4, nullptr, LX_IID_IDxcResult, (void **)&res);
        if (SUCCEEDED(hr) && res)
        {
            HRESULT st = E_FAIL; res->GetStatus(&st);
            res->GetResult(&dxbc);
            bool ok = (st == S_OK) && dxbc && dxbc->GetBufferSize() >= 4 &&
                      memcmp(dxbc->GetBufferPointer(), "DXBC", 4) == 0;
            check("compile.ps_5_0.dxbc", ok, "status=0x%08lX size=%lu",
                  (unsigned long)st, dxbc ? (unsigned long)dxbc->GetBufferSize() : 0UL);
            res->Release();
        }
        else check("compile.ps_5_0.dxbc", false, "Compile hr=0x%08lX", (unsigned long)hr);
    }
    else skip("compile.ps_5_0.dxbc", "no compiler");

    /* 4. compile HLSL -> SM6 : defined refusal, no output */
    if (comp)
    {
        LPCWSTR args6[] = { L"-T", L"ps_6_0", L"-E", L"main" };
        IDxcResult *res = nullptr;
        hr = comp->Compile(&src, args6, 4, nullptr, LX_IID_IDxcResult, (void **)&res);
        if (SUCCEEDED(hr) && res)
        {
            HRESULT st = S_OK; res->GetStatus(&st);
            IDxcBlob *obj = nullptr; res->GetResult(&obj);
            IDxcBlobEncoding *err = nullptr; res->GetErrorBuffer(&err);
            bool ok = (st == LX_DXC_E_SM6_UNSUPPORTED) && !obj &&
                      err && err->GetBufferSize() > 0;
            check("compile.ps_6_0.refused", ok, "status=0x%08lX (want 0x%08lX) err=%lu",
                  (unsigned long)st, (unsigned long)LX_DXC_E_SM6_UNSUPPORTED,
                  err ? (unsigned long)err->GetBufferSize() : 0UL);
            if (obj) obj->Release();
            if (err) err->Release();
            res->Release();
        }
        else check("compile.ps_6_0.refused", false, "Compile hr=0x%08lX", (unsigned long)hr);
    }
    else skip("compile.ps_6_0.refused", "no compiler");

    /* 5. disassemble the DXBC produced above -> ASM text */
    if (comp && dxbc)
    {
        DxcBuffer ob; ob.Ptr = dxbc->GetBufferPointer(); ob.Size = dxbc->GetBufferSize(); ob.Encoding = 0;
        IDxcResult *res = nullptr;
        hr = comp->Disassemble(&ob, LX_IID_IDxcResult, (void **)&res);
        if (SUCCEEDED(hr) && res)
        {
            HRESULT st = E_FAIL; res->GetStatus(&st);
            IDxcBlob *asm_ = nullptr; res->GetResult(&asm_);
            bool ok = (st == S_OK) && asm_ && asm_->GetBufferSize() > 0;
            check("disassemble.dxbc.asm", ok, "status=0x%08lX size=%lu",
                  (unsigned long)st, asm_ ? (unsigned long)asm_->GetBufferSize() : 0UL);
            if (asm_) asm_->Release();
            res->Release();
        }
        else check("disassemble.dxbc.asm", false, "Disassemble hr=0x%08lX", (unsigned long)hr);
    }
    else skip("disassemble.dxbc.asm", "no DXBC from compile step");

    /* 6. validator returns a well-formed result object (structural; DXBC is not
     *    DXIL, so a non-S_OK status is acceptable - we assert the contract, not
     *    a positive verdict, since a signed-DXIL fixture is out of CPU scope). */
    if (val && dxbc)
    {
        IDxcOperationResult *vres = nullptr;
        hr = val->Validate(dxbc, 0, &vres);
        if (vres)
        {
            HRESULT st = 0; HRESULT ghr = vres->GetStatus(&st);
            check("validate.result.wellformed", SUCCEEDED(ghr),
                  "GetStatus hr=0x%08lX validate.status=0x%08lX", (unsigned long)ghr, (unsigned long)st);
            vres->Release();
        }
        else check("validate.result.wellformed", false, "Validate hr=0x%08lX no result", (unsigned long)hr);
    }
    else skip("validate.result.wellformed", "no validator or DXBC");

    /* 7. utils blob round-trip (byte-exact) */
    if (utils)
    {
        const char payload[] = "LiberX-DXC-blob";
        IDxcBlobEncoding *b = nullptr;
        hr = utils->CreateBlob(payload, (UINT32)sizeof(payload), DXC_CP_UTF8, &b);
        bool ok = SUCCEEDED(hr) && b && b->GetBufferSize() >= sizeof(payload) &&
                  memcmp(b->GetBufferPointer(), payload, sizeof(payload)) == 0;
        check("utils.createblob.roundtrip", ok, "hr=0x%08lX", (unsigned long)hr);
        if (b) b->Release();
    }
    else skip("utils.createblob.roundtrip", "no utils");

    if (dxbc) dxbc->Release();
    if (comp) comp->Release();
    if (utils) utils->Release();
    if (val) val->Release();
    FreeLibrary(h);

    printf("DXC-DIFF-RESULT PASS=%d FAIL=%d SKIP=%d\n", g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
