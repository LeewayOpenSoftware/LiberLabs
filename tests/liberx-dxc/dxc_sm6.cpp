/*
 * PROJECT:     LiberX DXC Level 2 - Shader Model 6 functional test (CPU-only)
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Drive dxcompiler.dll through its public COM surface with a
 *              directory of HLSL fixtures (sm6/CASES.txt): compile each one to
 *              a DXIL container, check the container parts, validate it with
 *              IDxcValidator, disassemble it, and keep the object for host-side
 *              inspection. Negative fixtures must fail with diagnostics and no
 *              object. Loads the DLL at runtime; no GPU, no device.
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *                <281492504+LeewayOpenSoftware@users.noreply.github.com>
 */

#include <windows.h>
#include <unknwn.h>
#include <dxcapi.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>

static const IID LX_IID_IDxcCompiler3 =
    {0x228B4687,0x5A6A,0x4730,{0x90,0x0C,0x97,0x02,0xB2,0x20,0x3F,0x54}};
static const IID LX_IID_IDxcValidator =
    {0xA6E82BD2,0x1FD7,0x4826,{0x98,0x11,0x28,0x57,0xE7,0x97,0xF4,0x9A}};
static const IID LX_IID_IDxcResult =
    {0x58346CDA,0xDDE7,0x4497,{0x94,0x61,0x6F,0x87,0xAF,0x5E,0x06,0x59}};

static int g_pass, g_fail, g_skip;

static void check(const char *name, bool ok, const char *fmt, ...)
{
    char detail[512];
    va_list a; va_start(a, fmt); vsnprintf(detail, sizeof detail, fmt, a); va_end(a);
    printf("DXC-SM6 %s %s %s\n", name, ok ? "PASS" : "FAIL", detail);
    if (ok) ++g_pass; else ++g_fail;
}
static void skip(const char *name, const char *why)
{
    printf("DXC-SM6 %s SKIP %s\n", name, why);
    ++g_skip;
}

typedef HRESULT (__stdcall *DxcCreateInstanceProc)(REFCLSID, REFIID, LPVOID *);

static char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(n > 0 ? (size_t)n + 1 : 1);
    size_t got = n > 0 ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    buf[got] = 0;
    *size = got;
    return buf;
}

static void write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, size, f);
    fclose(f);
}

/* DXIL container: "DXBC", 16-byte hash, version, size, part count, offsets. */
struct container_info
{
    bool magic_ok;
    bool has_dxil;
    bool hash_zero;
    unsigned parts;
    char fourccs[16 * 5 + 1];
};

static void inspect_container(const unsigned char *p, size_t size, container_info *ci)
{
    memset(ci, 0, sizeof *ci);
    if (size < 32 || memcmp(p, "DXBC", 4) != 0) return;
    ci->magic_ok = true;
    ci->hash_zero = true;
    for (unsigned i = 4; i < 20; ++i)
        if (p[i]) ci->hash_zero = false;
    unsigned count = *(const unsigned *)(p + 28);
    if (32 + (size_t)count * 4 > size) return;
    ci->parts = count;
    size_t pos = 0;
    for (unsigned i = 0; i < count && i < 16; ++i)
    {
        unsigned off = *(const unsigned *)(p + 32 + i * 4);
        if ((size_t)off + 8 > size) break;
        const char *fourcc = (const char *)p + off;
        if (memcmp(fourcc, "DXIL", 4) == 0) ci->has_dxil = true;
        memcpy(ci->fourccs + pos, fourcc, 4);
        ci->fourccs[pos + 4] = ' ';
        pos += 5;
    }
    ci->fourccs[pos] = 0;
}

static void to_wide(const char *s, wchar_t *w, size_t cap)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; ++i) w[i] = (unsigned char)s[i];
    w[i] = 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "sm6";
    const char *out = argc > 2 ? argv[2] : nullptr;
    char path[1024];

    HMODULE h = LoadLibraryW(L"dxcompiler.dll");
    if (!h) { printf("DXC-SM6 load FAIL could not LoadLibrary dxcompiler.dll\n");
              printf("DXC-SM6-RESULT PASS=0 FAIL=1 SKIP=0\n"); return 1; }
    DxcCreateInstanceProc create =
        (DxcCreateInstanceProc)GetProcAddress(h, "DxcCreateInstance");
    check("entry.DxcCreateInstance", create != nullptr, "GetProcAddress");
    if (!create) { printf("DXC-SM6-RESULT PASS=%d FAIL=%d SKIP=%d\n", g_pass, g_fail, g_skip); return 1; }

    IDxcCompiler3 *comp = nullptr;
    HRESULT hr = create(CLSID_DxcCompiler, LX_IID_IDxcCompiler3, (void **)&comp);
    check("create.IDxcCompiler3", SUCCEEDED(hr) && comp, "hr=0x%08lX", (unsigned long)hr);
    IDxcValidator *val = nullptr;
    hr = create(CLSID_DxcValidator, LX_IID_IDxcValidator, (void **)&val);
    check("create.IDxcValidator", SUCCEEDED(hr) && val, "hr=0x%08lX", (unsigned long)hr);

    snprintf(path, sizeof path, "%s\\CASES.txt", dir);
    size_t cases_size = 0;
    char *cases = read_file(path, &cases_size);
    check("cases.read", cases != nullptr, "%s", path);
    if (!cases || !comp)
    {
        printf("DXC-SM6-RESULT PASS=%d FAIL=%d SKIP=%d\n", g_pass, g_fail, g_skip);
        return g_fail ? g_fail : 1;
    }

    /* CASES.txt: one case per line: <name> <profile> <entry> <ok|fail> [extra args...] */
    char *line = strtok(cases, "\r\n");
    while (line)
    {
        char *next = strtok(nullptr, "\r\n");
        if (line[0] == '#' || line[0] == 0) { line = next; continue; }

        char *tok[16]; int ntok = 0;
        for (char *t = line; *t && ntok < 16; )
        {
            while (*t == ' ' || *t == '\t') ++t;
            if (!*t) break;
            tok[ntok++] = t;
            while (*t && *t != ' ' && *t != '\t') ++t;
            if (*t) *t++ = 0;
        }
        if (ntok < 4) { line = next; continue; }
        const char *name = tok[0];
        bool expect_ok = strcmp(tok[3], "ok") == 0;

        char cname[128];
        snprintf(path, sizeof path, "%s\\%s.hlsl", dir, name);
        size_t src_size = 0;
        char *src = read_file(path, &src_size);
        snprintf(cname, sizeof cname, "%s.read", name);
        check(cname, src != nullptr, "%s (%lu bytes)", path, (unsigned long)src_size);
        if (!src) { line = next; continue; }

        wchar_t wprofile[64], wentry[64], wextra[8][64];
        to_wide(tok[1], wprofile, 64);
        to_wide(tok[2], wentry, 64);
        LPCWSTR args[16] = { L"-T", wprofile, L"-E", wentry };
        UINT32 nargs = 4;
        for (int i = 4; i < ntok && i - 4 < 8; ++i)
        {
            to_wide(tok[i], wextra[i - 4], 64);
            args[nargs++] = wextra[i - 4];
        }

        DxcBuffer buf; buf.Ptr = src; buf.Size = src_size; buf.Encoding = DXC_CP_UTF8;
        IDxcResult *res = nullptr;
        hr = comp->Compile(&buf, args, nargs, nullptr, LX_IID_IDxcResult, (void **)&res);
        snprintf(cname, sizeof cname, "%s.compile", name);
        if (FAILED(hr) || !res)
        {
            check(cname, false, "Compile hr=0x%08lX", (unsigned long)hr);
            free(src); line = next; continue;
        }
        HRESULT st = E_FAIL; res->GetStatus(&st);
        IDxcBlob *obj = nullptr; res->GetResult(&obj);
        IDxcBlobEncoding *err = nullptr; res->GetErrorBuffer(&err);
        size_t err_size = err ? err->GetBufferSize() : 0;
        const char *err_text = err && err_size ? (const char *)err->GetBufferPointer() : "";

        if (!expect_ok)
        {
            bool ok = FAILED(st) && (!obj || obj->GetBufferSize() == 0) && err_size > 1;
            check(cname, ok, "expected failure: status=0x%08lX object=%lu errors=%lu",
                  (unsigned long)st, obj ? (unsigned long)obj->GetBufferSize() : 0UL,
                  (unsigned long)err_size);
            if (err_size > 1) printf("DXC-SM6-DIAG %s: %.*s\n", name, (int)(err_size > 300 ? 300 : err_size), err_text);
        }
        else
        {
            bool ok = (st == S_OK) && obj && obj->GetBufferSize() > 32;
            check(cname, ok, "%s status=0x%08lX object=%lu errors=%lu", tok[1], (unsigned long)st,
                  obj ? (unsigned long)obj->GetBufferSize() : 0UL, (unsigned long)err_size);
            if (!ok && err_size > 1) printf("DXC-SM6-DIAG %s: %.*s\n", name, (int)(err_size > 600 ? 600 : err_size), err_text);
            if (ok)
            {
                container_info ci;
                inspect_container((const unsigned char *)obj->GetBufferPointer(), obj->GetBufferSize(), &ci);
                snprintf(cname, sizeof cname, "%s.container", name);
                check(cname, ci.magic_ok && ci.has_dxil, "parts=%u [%s] hash=%s",
                      ci.parts, ci.fourccs, ci.hash_zero ? "zero(unsigned)" : "nonzero(signed)");

                snprintf(cname, sizeof cname, "%s.validate", name);
                if (val)
                {
                    IDxcOperationResult *vres = nullptr;
                    HRESULT vhr = val->Validate(obj, 0, &vres);
                    if (vres)
                    {
                        HRESULT vst = E_FAIL; vres->GetStatus(&vst);
                        IDxcBlobEncoding *verr = nullptr; vres->GetErrorBuffer(&verr);
                        size_t vsz = verr ? verr->GetBufferSize() : 0;
                        check(cname, vst == S_OK, "status=0x%08lX errors=%lu", (unsigned long)vst, (unsigned long)vsz);
                        if (vst != S_OK && vsz > 1)
                            printf("DXC-SM6-DIAG %s validator: %.*s\n", name, (int)(vsz > 600 ? 600 : vsz), (const char *)verr->GetBufferPointer());
                        if (verr) verr->Release();
                        vres->Release();
                    }
                    else check(cname, false, "Validate hr=0x%08lX no result", (unsigned long)vhr);
                }
                else skip(cname, "no validator");

                snprintf(cname, sizeof cname, "%s.disassemble", name);
                DxcBuffer ob; ob.Ptr = obj->GetBufferPointer(); ob.Size = obj->GetBufferSize(); ob.Encoding = 0;
                IDxcResult *dres = nullptr;
                HRESULT dhr = comp->Disassemble(&ob, LX_IID_IDxcResult, (void **)&dres);
                if (SUCCEEDED(dhr) && dres)
                {
                    HRESULT dst = E_FAIL; dres->GetStatus(&dst);
                    IDxcBlob *text = nullptr; dres->GetResult(&text);
                    size_t tsz = text ? text->GetBufferSize() : 0;
                    bool has_ir = tsz > 0 && strstr((const char *)text->GetBufferPointer(), "define ") != nullptr;
                    check(cname, dst == S_OK && has_ir, "status=0x%08lX size=%lu has_define=%d",
                          (unsigned long)dst, (unsigned long)tsz, has_ir ? 1 : 0);
                    if (out && text && tsz)
                    {
                        snprintf(path, sizeof path, "%s\\%s.ll", out, name);
                        write_file(path, text->GetBufferPointer(), tsz);
                    }
                    if (text) text->Release();
                    dres->Release();
                }
                else check(cname, false, "Disassemble hr=0x%08lX", (unsigned long)dhr);

                if (out)
                {
                    snprintf(path, sizeof path, "%s\\%s.dxil", out, name);
                    write_file(path, obj->GetBufferPointer(), obj->GetBufferSize());
                }
            }
        }
        if (obj) obj->Release();
        if (err) err->Release();
        res->Release();
        free(src);
        line = next;
    }

    free(cases);
    if (comp) comp->Release();
    if (val) val->Release();
    FreeLibrary(h);

    printf("DXC-SM6-RESULT PASS=%d FAIL=%d SKIP=%d\n", g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
