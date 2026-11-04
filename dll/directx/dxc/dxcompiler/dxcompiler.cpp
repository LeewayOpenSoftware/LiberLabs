/*
 * PROJECT:     LiberX DirectX Shader Compiler (DXC) compatibility layer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 *              The compile/validate/reflect paths are backed by vkd3d-shader
 *              (LGPL-2.1-or-later). The DXC COM ABI (dxcapi.h) is vendored from
 *              Microsoft DirectXShaderCompiler under the University of Illinois/
 *              NCSA license; see sdk/include/psdk/dxcapi.h and
 *              shared/libernt/liberx-research/LICENSE-AUDIT-vkd3d.md.
 * PURPOSE:     Item 18 DXC Level 1 - an explicitly limited DXC compatibility
 *              milestone. Exposes IDxcCompiler3 / IDxcUtils / IDxcValidator.
 *              Capabilities are exposed ONLY where genuinely implemented and
 *              tested; everything else returns a defined HRESULT plus an error
 *              blob and NEVER misleading output. This build does NOT advertise
 *              or provide full Shader Model 6 compilation. SM6 compile requests
 *              fail explicitly and consistently (see Compile()).
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *                <281492504+LeewayOpenSoftware@users.noreply.github.com>
 *
 * B0 (this file): the DXC COM surface and object plumbing - DxcCreateInstance,
 * reference counting, QueryInterface, blob/result objects, and honest E_NOTIMPL
 * failures with error blobs for every capability. B1 wires the real
 * vkd3d-shader parse/validate/reflect/disassemble + HLSL->SM<=5 compile paths.
 */

#include <windows.h>
#include <unknwn.h>
#define DXC_API_IMPORT   /* we implement+export these (see .spec); not an import */
#include <dxcapi.h>
#include <cstring>
#include <cwchar>
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

/* HRESULT used for "this build does not implement that capability yet".
 * E_NOTIMPL is the defined, consistent failure code for the Level 1 skeleton. */
#define LX_DXC_E_NOTIMPL E_NOTIMPL

/* ------------------------------------------------------------------ *
 * Blob object - implements IDxcBlobUtf8 (covers IDxcBlob and          *
 * IDxcBlobEncoding by inheritance). Holds either binary bytes or a    *
 * NUL-terminated UTF-8 string.                                        *
 * ------------------------------------------------------------------ */
class DxcBlobImpl : public IDxcBlobUtf8
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
 * Carries a status HRESULT, an optional object blob, and an optional  *
 * UTF-8 error blob. Enough for honest Compile/Disassemble/Validate.   *
 * ------------------------------------------------------------------ */
class DxcResultImpl : public IDxcResult
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

/* Build an IDxcResult carrying a failed status + an error message, then QI it
 * to the caller-requested interface. Used for every unimplemented capability so
 * the failure is explicit, consistent, and never silent. */
static HRESULT MakeFailResult(HRESULT status, const char *msg,
                              DXC_OUT_KIND objKind, REFIID riid, void **ppResult)
{
    if (!ppResult) return E_POINTER;
    *ppResult = nullptr;
    DxcBlobImpl *err = NewTextBlob(msg);
    DxcResultImpl *res = new (std::nothrow) DxcResultImpl(status, nullptr, objKind, err);
    if (err) err->Release();
    if (!res) return E_OUTOFMEMORY;
    HRESULT hr = res->QueryInterface(riid, ppResult);
    res->Release();
    return hr;
}

/* ------------------------------------------------------------------ *
 * IDxcCompiler3                                                        *
 * ------------------------------------------------------------------ */
class DxcCompilerImpl : public IDxcCompiler3
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
        (void)pSource; (void)pArguments; (void)argCount; (void)pIncludeHandler;
        /* B0 skeleton: no code generation wired yet. Fail explicitly with an
         * error blob. B1 routes SM<=5 to vkd3d and returns a *defined* SM6
         * refusal here. Never emits misleading success. */
        return MakeFailResult(LX_DXC_E_NOTIMPL,
            "LiberX DXC Level 1 (B0): HLSL compilation is not wired in this "
            "build yet. This build never produces Shader Model 6 DXIL.",
            DXC_OUT_OBJECT, riid, (void **)ppResult);
    }

    HRESULT STDMETHODCALLTYPE Disassemble(const DxcBuffer *pObject,
                                          REFIID riid, LPVOID *ppResult) override
    {
        (void)pObject;
        return MakeFailResult(LX_DXC_E_NOTIMPL,
            "LiberX DXC Level 1 (B0): disassembly is not wired in this build "
            "yet.", DXC_OUT_DISASSEMBLY, riid, (void **)ppResult);
    }
};

/* ------------------------------------------------------------------ *
 * IDxcUtils                                                            *
 * ------------------------------------------------------------------ */
class DxcUtilsImpl : public IDxcUtils
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

    /* Implemented in B0: byte-for-byte blob creation (no compiler needed). */
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
        *ppBlobEncoding = b;    /* holds the single reference */
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlobFromPinned(LPCVOID pData, UINT32 size,
                                                   UINT32 codePage,
                                                   IDxcBlobEncoding **ppBlobEncoding) override
    {
        /* We copy rather than pin; semantically a superset (safe). */
        return CreateBlob(pData, size, codePage, ppBlobEncoding);
    }
    HRESULT STDMETHODCALLTYPE GetBlobAsUtf8(IDxcBlob *pBlob,
                                            IDxcBlobUtf8 **ppBlobEncoding) override
    {
        if (!ppBlobEncoding) return E_POINTER;
        *ppBlobEncoding = nullptr;
        if (!pBlob) return E_INVALIDARG;
        /* B0: only honor blobs that are already UTF-8 (no transcoding yet). */
        IDxcBlobUtf8 *u = nullptr;
        if (SUCCEEDED(pBlob->QueryInterface(LX_IID_IDxcBlobUtf8, (void **)&u)) && u)
        { *ppBlobEncoding = u; return S_OK; }
        return LX_DXC_E_NOTIMPL;   /* transcoding wired in B1 */
    }

    /* Not implemented in B0 - honest E_NOTIMPL, never misleading. */
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
 * IDxcValidator                                                        *
 * ------------------------------------------------------------------ */
class DxcValidatorImpl : public IDxcValidator
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
        (void)pShader; (void)Flags;
        return MakeFailResult(LX_DXC_E_NOTIMPL,
            "LiberX DXC Level 1 (B0): DXIL validation is not wired in this "
            "build yet.", DXC_OUT_NONE, LX_IID_IDxcOperationResult,
            (void **)ppResult);
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
    (void)pMalloc;   /* custom allocator not honored in B0 */
    return CreateObject(rclsid, riid, ppv);
}
