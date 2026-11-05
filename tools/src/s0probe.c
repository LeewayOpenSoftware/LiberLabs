/* SPDX-License-Identifier: GPL-2.0-or-later */
/* S0 diagnostic probe: system cert store enumeration + test-CA installation.
 * Diagnostic only; uses only public crypt32 APIs. */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include "tlspoc_testcert.h"

static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    fflush(stdout);
    va_end(ap);
}

static DWORD count_store(LPCWSTR name)
{
    HCERTSTORE st = CertOpenSystemStoreW(0, name);
    DWORD count = 0;
    PCCERT_CONTEXT cert = NULL;

    if (!st)
    {
        logf("store %-8S: OPEN FAILED gle=%lu\n", name, GetLastError());
        return (DWORD)-1;
    }
    while ((cert = CertEnumCertificatesInStore(st, cert)))
        count++;
    CertCloseStore(st, 0);
    return count;
}

static int install_into(LPCWSTR name)
{
    HCERTSTORE st;
    PCCERT_CONTEXT ctx;
    BOOL ok;

    BYTE der[4096];
    DWORD der_len = sizeof(der);

    st = CertOpenSystemStoreW(0, name);
    if (!st)
    {
        logf("install: open %S failed gle=%lu\n", name, GetLastError());
        return 1;
    }
    if (!CryptStringToBinaryA(tlspoc_cert_pem, tlspoc_cert_pem_len, CRYPT_STRING_BASE64HEADER,
                              der, &der_len, NULL, NULL))
    {
        logf("install: CryptStringToBinary failed gle=%lu\n", GetLastError());
        CertCloseStore(st, 0);
        return 1;
    }
    ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der, der_len);
    if (!ctx)
    {
        logf("install: CertCreateCertificateContext(DER,%lu) failed gle=%lu\n", der_len, GetLastError());
        CertCloseStore(st, 0);
        return 1;
    }
    ok = CertAddCertificateContextToStore(st, ctx, CERT_STORE_ADD_REPLACE_EXISTING, NULL);
    logf("install: CertAddCertificateContextToStore(%S) -> %d gle=%lu\n", name, ok, ok ? 0 : GetLastError());
    CertFreeCertificateContext(ctx);
    CertCloseStore(st, 0);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    logf("S0PROBE start\n");
    logf("ROOT count=%lu\n", count_store(L"ROOT"));
    logf("AuthRoot count=%lu\n", count_store(L"AuthRoot"));
    logf("CA count=%lu\n", count_store(L"CA"));

    if (argc >= 3 && !_stricmp(argv[1], "install"))
    {
        LPWSTR wide = NULL;
        int rc;
        if (!_stricmp(argv[2], "ROOT"))
            rc = install_into(L"ROOT");
        else if (!_stricmp(argv[2], "AuthRoot"))
            rc = install_into(L"AuthRoot");
        else
        {
            logf("install: unknown store %s\n", argv[2]);
            rc = 2;
        }
        (void)wide;
        logf("post-install ROOT count=%lu\n", count_store(L"ROOT"));
        logf("post-install AuthRoot count=%lu\n", count_store(L"AuthRoot"));
        logf("S0PROBE end rc=%d\n", rc);
        return rc;
    }
    logf("S0PROBE end\n");
    return 0;
}
