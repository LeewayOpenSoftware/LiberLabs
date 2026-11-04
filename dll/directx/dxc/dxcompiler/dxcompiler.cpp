/*
 * PROJECT:     LiberX DirectX Shader Compiler (DXC) compatibility layer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 *              The compile/validate/disassemble paths are backed by vkd3d-shader
 *              (LGPL-2.1-or-later). The DXC COM ABI (dxcapi.h) is vendored from
 *              Microsoft DirectXShaderCompiler under the University of Illinois/
 *              NCSA license; see sdk/include/psdk/dxcapi.h and
 *              shared/libernt/liberx-research/LICENSE-AUDIT-vkd3d.md.
 * PURPOSE:     Item 18 DXC Level 1 - an explicitly limited DXC compatibility
 *              milestone. Exposes IDxcCompiler3 / IDxcUtils / IDxcValidator.
 *              Capabilities are exposed ONLY where genuinely implemented and
 *              tested, kept distinct, and this build NEVER advertises or
 *              produces full Shader Model 6 DXIL. An SM6 compile request fails
 *              explicitly and consistently (defined HRESULT + error blob).
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *                <281492504+LeewayOpenSoftware@users.noreply.github.com>
 *
 * Capability matrix (what is genuinely implemented here):
 *   IDxcCompiler3::Compile  - HLSL -> Shader Model <= 5 (DXBC/TPF) via vkd3d.
 *                             SM6 (profile *_6_*) -> defined refusal, no output.
 *   IDxcCompiler3::Disassemble - DXBC/DXIL container -> D3D ASM text via vkd3d.
 *   IDxcValidator::Validate - structural (UNSIGNED) DXIL validation via vkd3d
 *                             parse. No Microsoft dxil.dll signing is performed.
 *   IDxcUtils               - blob creation (real); reflection / container-part
 *                             extraction NOT yet implemented (honest E_NOTIMPL).
 */

#include <windows.h>
#include <unknwn.h>
#define DXC_API_IMPORT   /* we implement+export these (see .spec); not an import */
#include <dxcapi.h>
#include <vkd3d_shader.h>
#include <cstring>
#include <cwchar>
#include <cstdio>
#include <new>

/* ------------------------------------------------------------------ *
 * Interface IIDs (defined locally from the public DXC ABI strings so  *
 * we never depend on __uuidof being available in the toolchain).      *
 * ------------------------------------------------------------------ */
static const IID LX_IID_IDxcBlob =
    {0x8BA5FB08,0x5195,0x40e2,{0xAC,0x58,0x0D,0x98,0x9C,0x3A,0x01,0x02}};
static const IID LX_IID_IDxcBlobEncoding =
    {0x7241d424,0x2646,0x4191,{0x97,0xc0,0x98,0xe9,0x6e,0x42,0xfc,0x68}};
static const IID LX_IID_IDxcBlobUtf8 =
    {0x3DA636C9,0xBA71,0x4024,{0xA3,0x01,0x30,0xCB,0xF1,0x25,0x30,0x5B}};
static const IID LX_IID_IDxcOperationResult =
    {0xCEDB484A,0xD4E9,0x445A,{0xB9,0x91,0xCA,0x21,0xCA,0x15,0x7D,0xC2}};
static const IID LX_IID_IDxcResult =
    {0x58346CDA,0xDDE7,0x4497,{0x94,0x61,0x6F,0x87,0xAF,0x5E,0x06,0x59}};
static const IID LX_IID_IDxcCompiler3 =
    {0x228B4687,0x5A6A,0x4730,{0x90,0x0C,0x97,0x02,0xB2,0x20,0x3F,0x54}};
static const IID LX_IID_IDxcUtils =
    {0x4605C4CB,0x2019,0x492A,{0xAD,0xA4,0x65,0xF2,0x0B,0xB7,0xD6,0x7F}};
static const IID LX_IID_IDxcValidator =
    {0xA6E82BD2,0x1FD7,0x4826,{0x98,0x11,0x28,0x57,0xE7,0x97,0xF4,0x9A}};

static inline bool IidEq(REFIID a, REFIID b) { return IsEqualIID(a, b) != FALSE; }

/* Defined, consistent failure codes (owner req: SM6 must fail with a defined
 * HRESULT plus an error blob). Custom facility 0xAA = "LiberX DXC". */
#define LX_DXC_E_NOTIMPL         E_NOTIMPL
#define LX_DXC_E_SM6_UNSUPPORTED ((HRESULT)0x80AA0006L)

/* ------------------------------------------------------------------ *
 * Blob object - implements IDxcBlobUtf8 (covers IDxcBlob and          *
 * IDxcBlobEncoding by inheritance). Holds either binary bytes or a    *
 * NUL-terminated UTF-8 string.                                        *
 * ------------------------------------------------------------------ */
class DxcBlobImpl final : public IDxcBlobUtf8
{
    LONG   m_ref;
    void  *m_buf;        /* CoTaskMemAlloc'd */
    SIZE_T m_bufSize;    /* bytes returned by GetBufferSize (incl. NUL if text) */
    SIZE_T m_strLen;     /* chars excluding NUL (text only) */
    BOOL   m_known;      /* encoding known? */
    UINT32 m_cp;         /* code page */
    bool   m_text;

public:
    DxcBlobImpl(const void *data, SIZE_T size, UINT32 codePage, bool text)
        : m_ref(1), m_buf(nullptr), m_bufSize(0), m_strLen(0),
          m_known(text ? TRUE : FALSE), m_cp(text ? codePage : CP_ACP), m_text(text)
    {
        SIZE_T alloc = size + (text ? 1 : 0);
        if (alloc == 0) alloc = 1;
        m_buf = CoTaskMemAlloc(alloc);
        if (m_buf)
        {
            if (data && size) memcpy(m_buf, data, size);
            if (text) ((char *)m_buf)[size] = '\0';
            m_bufSize = size + (text ? 1 : 0);
            m_strLen  = text ? size : 0;
        }
    }
    ~DxcBlobImpl() { if (m_buf) CoTaskMemFree(m_buf); }

    bool valid() const { return m_buf != nullptr; }

    /* IUnknown */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IidEq(riid, IID_IUnknown) || IidEq(riid, LX_IID_IDxcBlob) ||
            IidEq(riid, LX_IID_IDxcBlobEncoding))
        {
            *ppv = static_cast<IDxcBlob *>(this); AddRef(); return S_OK;
        }
        if (IidEq(riid, LX_IID_IDxcBlobUtf8))
        {
            if (!m_text) { *ppv = nullptr; return E_NOINTERFACE; }
            *ppv = static_cast<IDxcBlobUtf8 *>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release(void) override
    {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    /* IDxcBlob */
    LPVOID STDMETHODCALLTYPE GetBufferPointer(void) override { return m_buf; }
    SIZE_T STDMETHODCALLTYPE GetBufferSize(void) override { return m_bufSize; }

    /* IDxcBlobEncoding */
    HRESULT STDMETHODCALLTYPE GetEncoding(BOOL *pKnown, UINT32 *pCodePage) override
    {
        if (pKnown)    *pKnown = m_known;
        if (pCodePage) *pCodePage = m_cp;
        return S_OK;
    }

    /* IDxcBlobUtf8 */
    LPCSTR STDMETHODCALLTYPE GetStringPointer(void) override
    {
        return m_text ? (LPCSTR)m_buf : "";
    }
    SIZE_T STDMETHODCALLTYPE GetStringLength(void) override { return m_strLen; }
};

static DxcBlobImpl *NewTextBlob(const char *utf8)
{
    SIZE_T n = utf8 ? strlen(utf8) : 0;
    DxcBlobImpl *b = new (std::nothrow) DxcBlobImpl(utf8, n, DXC_CP_UTF8, true);
    if (b && !b->valid()) { b->Release(); b = nullptr; }
    return b;
}

/* ------------------------------------------------------------------ *
 * Result object - implements IDxcResult (covers IDxcOperationResult). *
 * ------------------------------------------------------------------ */
class DxcResultImpl final : public IDxcResult
{
    LONG         m_ref;
    HRESULT      m_status;
    IDxcBlob    *m_object;   /* DXC_OUT_OBJECT / disassembly, may be null */
    DXC_OUT_KIND m_objectKind;
    IDxcBlobUtf8 *m_errors;  /* DXC_OUT_ERRORS, may be null */

public:
    DxcResultImpl(HRESULT status, IDxcBlob *object, DXC_OUT_KIND objectKind,
                  IDxcBlobUtf8 *errors)
        : m_ref(1), m_status(status), m_object(object),
          m_objectKind(objectKind), m_errors(errors)
    {
        if (m_object) m_object->AddRef();
        if (m_errors) m_errors->AddRef();
    }
    ~DxcResultImpl()
    {
        if (m_object) m_object->Release();
        if (m_errors) m_errors->Release();
    }

    /* IUnknown */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IidEq(riid, IID_IUnknown) || IidEq(riid, LX_IID_IDxcOperationResult) ||
            IidEq(riid, LX_IID_IDxcResult))
        {
            *ppv = static_cast<IDxcResult *>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release(void) override
    {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    /* IDxcOperationResult */
    HRESULT STDMETHODCALLTYPE GetStatus(HRESULT *pStatus) override
    {
        if (!pStatus) return E_POINTER;
        *pStatus = m_status; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResult(IDxcBlob **ppResult) override
    {
        if (!ppResult) return E_POINTER;
        *ppResult = m_object;
        if (m_object) m_object->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetErrorBuffer(IDxcBlobEncoding **ppErrors) override
    {
        if (!ppErrors) return E_POINTER;
        *ppErrors = m_errors;
        if (m_errors) m_errors->AddRef();
        return S_OK;
    }

    /* IDxcResult */
    BOOL STDMETHODCALLTYPE HasOutput(DXC_OUT_KIND k) override
    {
        if (k == DXC_OUT_ERRORS) return m_errors != nullptr;
        if (m_object && k == m_objectKind) return TRUE;
        return FALSE;
    }
    HRESULT STDMETHODCALLTYPE GetOutput(DXC_OUT_KIND k, REFIID iid,
                                        void **ppvObject,
                                        IDxcBlobWide **ppOutputName) override
    {
        if (ppOutputName) *ppOutputName = nullptr;
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (k == DXC_OUT_ERRORS && m_errors)
            return m_errors->QueryInterface(iid, ppvObject);
        if (m_object && k == m_objectKind)
            return m_object->QueryInterface(iid, ppvObject);
        return E_INVALIDARG;
    }
    UINT32 GetNumOutputs() override
    {
        UINT32 n = 0;
        if (m_object) n++;
        if (m_errors) n++;
        return n;
    }
    DXC_OUT_KIND GetOutputByIndex(UINT32 i) override
    {
        UINT32 idx = 0;
        if (m_object) { if (idx == i) return m_objectKind; idx++; }
        if (m_errors) { if (idx == i) return DXC_OUT_ERRORS; idx++; }
        return DXC_OUT_NONE;
    }
    DXC_OUT_KIND PrimaryOutput() override
    {
        return m_object ? m_objectKind : DXC_OUT_NONE;
    }
};

/* ------------------------------------------------------------------ *
 * Result builders.                                                    *
 * ------------------------------------------------------------------ */
static HRESULT MakeObjectResult(HRESULT status, DxcBlobImpl *object,
                                DXC_OUT_KIND kind, const char *errMsg,
                                REFIID riid, void **ppResult)
{
    if (!ppResult) return E_POINTER;
    *ppResult = nullptr;
    DxcBlobImpl *err = (errMsg && errMsg[0]) ? NewTextBlob(errMsg) : nullptr;
    DxcResultImpl *res = new (std::nothrow) DxcResultImpl(status, object, kind, err);
    if (err) err->Release();
    if (!res) return E_OUTOFMEMORY;
    HRESULT hr = res->QueryInterface(riid, ppResult);
    res->Release();
    return hr;
}

/* Failed status + error message, no object. */
static HRESULT MakeFailResult(HRESULT status, const char *msg,
                              DXC_OUT_KIND objKind, REFIID riid, void **ppResult)
{
    return MakeObjectResult(status, nullptr, objKind, msg, riid, ppResult);
}

/* ------------------------------------------------------------------ *
 * Small helpers for the vkd3d-backed paths.                           *
 * ------------------------------------------------------------------ */
static char *Utf8FromWide(LPCWSTR w)   /* caller delete[]; null -> null */
{
    if (!w) return nullptr;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return nullptr;
    char *s = new (std::nothrow) char[n];
    if (!s) return nullptr;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, nullptr, nullptr);
    return s;
}

/* Return the value following a flag (e.g. L"-T") in the argv array, or null. */
static LPCWSTR FindArgValue(LPCWSTR *argv, UINT32 argc, LPCWSTR flag)
{
    if (!argv) return nullptr;
    for (UINT32 i = 0; i + 1 < argc; i++)
        if (argv[i] && wcscmp(argv[i], flag) == 0)
            return argv[i + 1];
    return nullptr;
}

/* Parse the Shader Model MAJOR version from a profile like "vs_6_0" -> 6. */
static int ProfileSMMajor(const char *profile)
{
    if (!profile) return -1;
    const char *u = strchr(profile, '_');
    if (!u) return -1;
    u++;
    if (*u < '0' || *u > '9') return -1;
    int maj = 0;
    while (*u >= '0' && *u <= '9') { maj = maj * 10 + (*u - '0'); u++; }
    return maj;
}

/* Decode a DxcBuffer source into a UTF-8 byte buffer (caller delete[]). */
static char *SourceToUtf8(const DxcBuffer *b, size_t *outLen)
{
    *outLen = 0;
    if (!b || !b->Ptr) return nullptr;
    if (b->Encoding == DXC_CP_UTF16)
    {
        int wn = (int)(b->Size / sizeof(wchar_t));
        int n = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)b->Ptr, wn,
                                    nullptr, 0, nullptr, nullptr);
        if (n <= 0) return nullptr;
        char *s = new (std::nothrow) char[n ? n : 1];
        if (!s) return nullptr;
        WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)b->Ptr, wn, s, n, nullptr, nullptr);
        *outLen = (size_t)n;
        return s;
    }
    char *s = new (std::nothrow) char[b->Size ? b->Size : 1];
    if (!s) return nullptr;
    if (b->Size) memcpy(s, b->Ptr, b->Size);
    *outLen = b->Size;
    return s;
}

/* ------------------------------------------------------------------ *
 * IDxcCompiler3                                                        *
 * ------------------------------------------------------------------ */
class DxcCompilerImpl final : public IDxcCompiler3
{
    LONG m_ref;
public:
    DxcCompilerImpl() : m_ref(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IidEq(riid, IID_IUnknown) || IidEq(riid, LX_IID_IDxcCompiler3))
        { *ppv = static_cast<IDxcCompiler3 *>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release(void) override
    { LONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return (ULONG)r; }

    HRESULT STDMETHODCALLTYPE Compile(const DxcBuffer *pSource,
                                      LPCWSTR *pArguments, UINT32 argCount,
                                      IDxcIncludeHandler *pIncludeHandler,
                                      REFIID riid, LPVOID *ppResult) override
    {
        (void)pIncludeHandler;   /* -I/include handling: B2 */
        if (!ppResult) return E_POINTER;
        *ppResult = nullptr;
        if (!pSource || !pSource->Ptr)
            return MakeFailResult(E_INVALIDARG, "LiberX DXC: no source provided.",
                                  DXC_OUT_OBJECT, riid, (void **)ppResult);

        LPCWSTR wprofile = FindArgValue(pArguments, argCount, L"-T");
        if (!wprofile) wprofile = FindArgValue(pArguments, argCount, L"/T");
        char *profile = Utf8FromWide(wprofile);
        if (!profile)
            return MakeFailResult(E_INVALIDARG,
                "LiberX DXC: no target profile (-T) specified.",
                DXC_OUT_OBJECT, riid, (void **)ppResult);

        int maj = ProfileSMMajor(profile);
        if (maj >= 6)
        {
            char msg[320];
            snprintf(msg, sizeof msg,
                "LiberX DXC Level 1: Shader Model 6 compilation (profile '%s') "
                "is not supported by this build. It compiles Shader Model 5 and "
                "below (HLSL->DXBC) via vkd3d, and never produces DXIL.", profile);
            delete[] profile;
            return MakeFailResult(LX_DXC_E_SM6_UNSUPPORTED, msg,
                                  DXC_OUT_OBJECT, riid, (void **)ppResult);
        }

        LPCWSTR wentry = FindArgValue(pArguments, argCount, L"-E");
        char *entry = Utf8FromWide(wentry);   /* null -> vkd3d uses "main" */

        size_t srcLen = 0;
        char *src = SourceToUtf8(pSource, &srcLen);
        if (!src)
        {
            delete[] profile; delete[] entry;
            return MakeFailResult(E_OUTOFMEMORY, "LiberX DXC: source decode failed.",
                                  DXC_OUT_OBJECT, riid, (void **)ppResult);
        }

        struct vkd3d_shader_hlsl_source_info hlsl;
        memset(&hlsl, 0, sizeof hlsl);
        hlsl.type        = VKD3D_SHADER_STRUCTURE_TYPE_HLSL_SOURCE_INFO;
        hlsl.entry_point = entry;
        hlsl.profile     = profile;

        struct vkd3d_shader_compile_info ci;
        memset(&ci, 0, sizeof ci);
        ci.type        = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
        ci.next        = &hlsl;
        ci.source.code = src;
        ci.source.size = srcLen;
        ci.source_type = VKD3D_SHADER_SOURCE_HLSL;
        ci.target_type = VKD3D_SHADER_TARGET_DXBC_TPF;   /* SM<=5 object */
        ci.log_level   = VKD3D_SHADER_LOG_WARNING;

        struct vkd3d_shader_code out;
        memset(&out, 0, sizeof out);
        char *messages = nullptr;
        int rc = vkd3d_shader_compile(&ci, &out, &messages);

        HRESULT hr;
        if (rc == VKD3D_OK)
        {
            DxcBlobImpl *obj = new (std::nothrow)
                DxcBlobImpl(out.code, out.size, DXC_CP_ACP, false);
            if (obj && obj->valid())
            {
                hr = MakeObjectResult(S_OK, obj, DXC_OUT_OBJECT, messages,
                                      riid, (void **)ppResult);
                obj->Release();
            }
            else
            {
                if (obj) obj->Release();
                hr = MakeFailResult(E_OUTOFMEMORY,
                    "LiberX DXC: out of memory building result.",
                    DXC_OUT_OBJECT, riid, (void **)ppResult);
            }
        }
        else
        {
            hr = MakeFailResult(E_FAIL,
                (messages && messages[0]) ? messages
                                          : "LiberX DXC: HLSL compilation failed.",
                DXC_OUT_OBJECT, riid, (void **)ppResult);
        }

        if (messages) vkd3d_shader_free_messages(messages);
        if (rc == VKD3D_OK) vkd3d_shader_free_shader_code(&out);
        delete[] src; delete[] entry; delete[] profile;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE Disassemble(const DxcBuffer *pObject,
                                          REFIID riid, LPVOID *ppResult) override
    {
        if (!ppResult) return E_POINTER;
        *ppResult = nullptr;
        if (!pObject || !pObject->Ptr || !pObject->Size)
            return MakeFailResult(E_INVALIDARG, "LiberX DXC: no object to disassemble.",
                                  DXC_OUT_DISASSEMBLY, riid, (void **)ppResult);

        /* Try a DXIL container first, then a TPF (SM<=5) container. */
        const enum vkd3d_shader_source_type tryTypes[2] =
            { VKD3D_SHADER_SOURCE_DXBC_DXIL, VKD3D_SHADER_SOURCE_DXBC_TPF };
        HRESULT hr = E_FAIL;
        bool done = false;
        for (int t = 0; t < 2 && !done; t++)
        {
            struct vkd3d_shader_compile_info ci;
            memset(&ci, 0, sizeof ci);
            ci.type        = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
            ci.source.code = pObject->Ptr;
            ci.source.size = pObject->Size;
            ci.source_type = tryTypes[t];
            ci.target_type = VKD3D_SHADER_TARGET_D3D_ASM;
            ci.log_level   = VKD3D_SHADER_LOG_WARNING;

            struct vkd3d_shader_code out;
            memset(&out, 0, sizeof out);
            char *messages = nullptr;
            int rc = vkd3d_shader_compile(&ci, &out, &messages);

            if (rc == VKD3D_OK)
            {
                DxcBlobImpl *obj = new (std::nothrow)
                    DxcBlobImpl(out.code, out.size, DXC_CP_UTF8, true);
                if (obj && obj->valid())
                {
                    hr = MakeObjectResult(S_OK, obj, DXC_OUT_DISASSEMBLY, messages,
                                          riid, (void **)ppResult);
                    obj->Release();
                }
                else
                {
                    if (obj) obj->Release();
                    hr = MakeFailResult(E_OUTOFMEMORY, "LiberX DXC: out of memory.",
                                        DXC_OUT_DISASSEMBLY, riid, (void **)ppResult);
                }
                done = true;
            }
            else if (t == 1)
            {
                hr = MakeFailResult(E_FAIL,
                    (messages && messages[0]) ? messages
                        : "LiberX DXC: disassembly failed (not a recognized "
                          "DXBC/DXIL container).",
                    DXC_OUT_DISASSEMBLY, riid, (void **)ppResult);
            }

            if (messages) vkd3d_shader_free_messages(messages);
            if (rc == VKD3D_OK) vkd3d_shader_free_shader_code(&out);
        }
        return hr;
    }
};

/* ------------------------------------------------------------------ *
 * IDxcUtils                                                            *
 * ------------------------------------------------------------------ */
class DxcUtilsImpl final : public IDxcUtils
{
    LONG m_ref;
public:
    DxcUtilsImpl() : m_ref(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IidEq(riid, IID_IUnknown) || IidEq(riid, LX_IID_IDxcUtils))
        { *ppv = static_cast<IDxcUtils *>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release(void) override
    { LONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return (ULONG)r; }

    /* Implemented: byte-exact blob creation (no compiler needed). */
    HRESULT STDMETHODCALLTYPE CreateBlob(LPCVOID pData, UINT32 size,
                                         UINT32 codePage,
                                         IDxcBlobEncoding **ppBlobEncoding) override
    {
        if (!ppBlobEncoding) return E_POINTER;
        *ppBlobEncoding = nullptr;
        bool text = (codePage != 0);
        DxcBlobImpl *b = new (std::nothrow) DxcBlobImpl(pData, size, codePage, text);
        if (!b) return E_OUTOFMEMORY;
        if (!b->valid()) { b->Release(); return E_OUTOFMEMORY; }
        *ppBlobEncoding = b;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlobFromPinned(LPCVOID pData, UINT32 size,
                                                   UINT32 codePage,
                                                   IDxcBlobEncoding **ppBlobEncoding) override
    {
        return CreateBlob(pData, size, codePage, ppBlobEncoding);   /* we copy */
    }
    HRESULT STDMETHODCALLTYPE GetBlobAsUtf8(IDxcBlob *pBlob,
                                            IDxcBlobUtf8 **ppBlobEncoding) override
    {
        if (!ppBlobEncoding) return E_POINTER;
        *ppBlobEncoding = nullptr;
        if (!pBlob) return E_INVALIDARG;
        IDxcBlobUtf8 *u = nullptr;
        if (SUCCEEDED(pBlob->QueryInterface(LX_IID_IDxcBlobUtf8, (void **)&u)) && u)
        { *ppBlobEncoding = u; return S_OK; }
        return LX_DXC_E_NOTIMPL;   /* transcoding: B2 */
    }

    /* Not implemented in Level 1 - honest E_NOTIMPL, never misleading. */
    HRESULT STDMETHODCALLTYPE CreateBlobFromBlob(IDxcBlob *, UINT32, UINT32,
                                                 IDxcBlob **pp) override
    { if (pp) *pp = nullptr; return LX_DXC_E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE MoveToBlob(LPCVOID, IMalloc *, UINT32, UINT32,
                                         IDxcBlobEncoding **pp) override
    { if (pp) *pp = nullptr; return LX_DXC_E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LoadFile(LPCWSTR, UINT32 *,
                                       IDxcBlobEncoding **pp) override
    { if (pp) *pp = nullptr; return LX_DXC_E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateReadOnlyStreamFromBlob(IDxcBlob *,
                                                           IStream **pp) override
    { if (pp) *pp = nullptr; return LX_DXC_E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateDefaultIncludeHandler(
        IDxcIncludeHandler **pp) override
    { if (pp) *pp = nullptr; return LX_DXC_E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetBlobAsWide(IDxcBlob *,
                                            IDxcBlobWide **pp) override
    { if (pp) *pp = nullptr; return LX_DXC_E_NOTIMPL; }
    /* Reflection / container-part extraction: not implemented in Level 1.
     * Kept DISTINCT from the implemented parse/disassemble/validate paths
     * (owner req #2) rather than faked. */
    HRESULT STDMETHODCALLTYPE GetDxilContainerPart(const DxcBuffer *, UINT32,
                                                   void **ppPartData,
                                                   UINT32 *pPartSizeInBytes) override
    { if (ppPartData) *ppPartData = nullptr; if (pPartSizeInBytes) *pPartSizeInBytes = 0;
      return LX_DXC_E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateReflection(const DxcBuffer *, REFIID,
                                               void **ppvReflection) override
    { if (ppvReflection) *ppvReflection = nullptr; return LX_DXC_E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BuildArguments(LPCWSTR, LPCWSTR, LPCWSTR,
                                             LPCWSTR *, UINT32,
                                             const DxcDefine *, UINT32,
                                             IDxcCompilerArgs **ppArgs) override
    { if (ppArgs) *ppArgs = nullptr; return LX_DXC_E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPDBContents(IDxcBlob *, IDxcBlob **ppHash,
                                             IDxcBlob **ppContainer) override
    { if (ppHash) *ppHash = nullptr; if (ppContainer) *ppContainer = nullptr;
      return LX_DXC_E_NOTIMPL; }
};

/* ------------------------------------------------------------------ *
 * IDxcValidator - structural, UNSIGNED DXIL validation via vkd3d.      *
 * No Microsoft dxil.dll signing/hash is performed.                    *
 * ------------------------------------------------------------------ */
class DxcValidatorImpl final : public IDxcValidator
{
    LONG m_ref;
public:
    DxcValidatorImpl() : m_ref(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IidEq(riid, IID_IUnknown) || IidEq(riid, LX_IID_IDxcValidator))
        { *ppv = static_cast<IDxcValidator *>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release(void) override
    { LONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return (ULONG)r; }

    HRESULT STDMETHODCALLTYPE Validate(IDxcBlob *pShader, UINT32 Flags,
                                       IDxcOperationResult **ppResult) override
    {
        (void)Flags;
        if (!ppResult) return E_POINTER;
        *ppResult = nullptr;
        if (!pShader || !pShader->GetBufferPointer() || !pShader->GetBufferSize())
            return MakeFailResult(E_INVALIDARG, "LiberX DXC: no shader to validate.",
                                  DXC_OUT_NONE, LX_IID_IDxcOperationResult,
                                  (void **)ppResult);

        /* Parse+translate the DXIL as a structural check. Success => structurally
         * valid (UNSIGNED - no MS dxil.dll hash). Failure => invalid. */
        struct vkd3d_shader_compile_info ci;
        memset(&ci, 0, sizeof ci);
        ci.type        = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
        ci.source.code = pShader->GetBufferPointer();
        ci.source.size = pShader->GetBufferSize();
        ci.source_type = VKD3D_SHADER_SOURCE_DXBC_DXIL;
        ci.target_type = VKD3D_SHADER_TARGET_D3D_ASM;
        ci.log_level   = VKD3D_SHADER_LOG_WARNING;

        struct vkd3d_shader_code out;
        memset(&out, 0, sizeof out);
        char *messages = nullptr;
        int rc = vkd3d_shader_compile(&ci, &out, &messages);

        HRESULT hr;
        if (rc == VKD3D_OK)
            hr = MakeObjectResult(S_OK, nullptr, DXC_OUT_NONE, messages,
                                  LX_IID_IDxcOperationResult, (void **)ppResult);
        else
            hr = MakeFailResult(E_FAIL,
                (messages && messages[0]) ? messages
                    : "LiberX DXC: DXIL structural validation failed.",
                DXC_OUT_NONE, LX_IID_IDxcOperationResult, (void **)ppResult);

        if (messages) vkd3d_shader_free_messages(messages);
        if (rc == VKD3D_OK) vkd3d_shader_free_shader_code(&out);
        return hr;
    }
};

/* ------------------------------------------------------------------ *
 * Class factory entry points (exported, see dxcompiler.spec).         *
 * ------------------------------------------------------------------ */
static HRESULT CreateObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    IUnknown *obj = nullptr;
    if (IsEqualCLSID(rclsid, CLSID_DxcCompiler))
        obj = static_cast<IDxcCompiler3 *>(new (std::nothrow) DxcCompilerImpl());
    else if (IsEqualCLSID(rclsid, CLSID_DxcUtils))      /* == CLSID_DxcLibrary */
        obj = static_cast<IDxcUtils *>(new (std::nothrow) DxcUtilsImpl());
    else if (IsEqualCLSID(rclsid, CLSID_DxcValidator))
        obj = static_cast<IDxcValidator *>(new (std::nothrow) DxcValidatorImpl());
    else
        return REGDB_E_CLASSNOTREG;

    if (!obj) return E_OUTOFMEMORY;
    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();   /* balance the constructor's ref=1 */
    return hr;
}

extern "C" HRESULT __stdcall
DxcCreateInstance(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    return CreateObject(rclsid, riid, ppv);
}

extern "C" HRESULT __stdcall
DxcCreateInstance2(IMalloc *pMalloc, REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    (void)pMalloc;   /* custom allocator not honored in Level 1 */
    return CreateObject(rclsid, riid, ppv);
}
