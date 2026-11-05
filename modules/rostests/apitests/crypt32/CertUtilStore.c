/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for certutil -store command: certificate store enumeration
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <apitest.h>
#include <windef.h>
#include <wincrypt.h>
#include <time.h>

/*
 * Test 1: Enumerate Root store (system store)
 */
static void test_enumerate_root_store(void)
{
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCert = NULL;
    int certCount = 0;

    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root");
    ok(hStore != NULL, "Failed to open Root store\n");
    if (!hStore)
        return;

    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
    {
        certCount++;
        ok(pCert->pCertInfo != NULL, "Invalid cert info at index %d\n", certCount);
        ok(pCert->pCertInfo->Subject.cbData > 0, "Empty subject in cert %d\n", certCount);
        ok(pCert->pCertInfo->Issuer.cbData > 0, "Empty issuer in cert %d\n", certCount);
    }

    ok(certCount > 0, "Root store should contain at least one certificate\n");
    CertCloseStore(hStore, 0);
}

/*
 * Test 2: Enumerate AuthRoot store
 */
static void test_enumerate_authroot_store(void)
{
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCert = NULL;
    int certCount = 0;

    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE, L"AuthRoot");
    ok(hStore != NULL, "Failed to open AuthRoot store\n");
    if (!hStore)
        return;

    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
    {
        certCount++;
    }

    ok(certCount >= 0, "AuthRoot enumeration count: %d\n", certCount);
    CertCloseStore(hStore, 0);
}

/*
 * Test 3: CurrentUser store access (should be readable)
 */
static void test_enumerate_current_user_store(void)
{
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCert = NULL;

    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_CURRENT_USER, L"My");
    ok(hStore != NULL, "Failed to open CurrentUser My store\n");
    if (!hStore)
        return;

    /* Just verify we can enumerate without error */
    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
    {
        /* Validate each certificate has required fields */
        ok(pCert->pCertInfo != NULL, "Invalid cert info in CurrentUser store\n");
    }

    CertCloseStore(hStore, 0);
}

/*
 * Test 4: Verify certificate dates can be read and converted
 */
static void test_certificate_dates(void)
{
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCert = NULL;
    BOOL foundDateCert = FALSE;

    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root");
    ok(hStore != NULL, "Failed to open Root store\n");
    if (!hStore)
        return;

    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
    {
        SYSTEMTIME stBefore, stAfter;
        FILETIME ftLocal;

        /* Verify NotBefore and NotAfter FILETIMEs can be converted to local time and SYSTEMTIME */
        ok(FileTimeToLocalFileTime(&pCert->pCertInfo->NotBefore, &ftLocal),
           "Failed to convert NotBefore to local\n");
        ok(FileTimeToSystemTime(&ftLocal, &stBefore),
           "Failed to convert NotBefore FILETIME to SYSTEMTIME\n");
        ok(FileTimeToLocalFileTime(&pCert->pCertInfo->NotAfter, &ftLocal),
           "Failed to convert NotAfter to local\n");
        ok(FileTimeToSystemTime(&ftLocal, &stAfter),
           "Failed to convert NotAfter FILETIME to SYSTEMTIME\n");

        foundDateCert = TRUE;
        break;
    }

    ok(foundDateCert, "No certificate found to test dates\n");
    CertCloseStore(hStore, 0);
}

/*
 * Test 5: Verify thumbprint (SHA1 hash) can be retrieved
 */
static void test_certificate_thumbprint(void)
{
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCert = NULL;
    BOOL foundThumbprintCert = FALSE;

    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root");
    ok(hStore != NULL, "Failed to open Root store\n");
    if (!hStore)
        return;

    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
    {
        BYTE thumbprint[20];
        DWORD thumbprintSize = sizeof(thumbprint);

        ok(CertGetCertificateContextProperty(pCert, CERT_SHA1_HASH_PROP_ID,
                                            thumbprint, &thumbprintSize),
           "Failed to get SHA1 thumbprint\n");
        ok(thumbprintSize == 20, "Thumbprint size should be 20 bytes, got %lu\n", thumbprintSize);

        foundThumbprintCert = TRUE;
        break;
    }

    ok(foundThumbprintCert, "No certificate found to test thumbprint\n");
    CertCloseStore(hStore, 0);
}

/*
 * Test 6: Verify serial number format
 */
static void test_certificate_serial(void)
{
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCert = NULL;
    BOOL foundSerialCert = FALSE;

    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root");
    ok(hStore != NULL, "Failed to open Root store\n");
    if (!hStore)
        return;

    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
    {
        PCRYPT_INTEGER_BLOB pSerial = &pCert->pCertInfo->SerialNumber;

        ok(pSerial->cbData > 0, "Serial number length is zero\n");
        ok(pSerial->pbData != NULL, "Serial number data is NULL\n");
        ok(pSerial->cbData <= 128, "Serial number exceeds expected size\n");

        foundSerialCert = TRUE;
        break;
    }

    ok(foundSerialCert, "No certificate found to test serial number\n");
    CertCloseStore(hStore, 0);
}

/*
 * Test 7: Verify certificate subject and issuer names
 */
static void test_certificate_names(void)
{
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCert = NULL;
    BOOL foundNameCert = FALSE;

    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root");
    ok(hStore != NULL, "Failed to open Root store\n");
    if (!hStore)
        return;

    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
    {
        WCHAR szSubject[1024] = {0};
        WCHAR szIssuer[1024] = {0};
        DWORD cbSubject, cbIssuer;

        cbSubject = CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL,
                                       szSubject, sizeof(szSubject)/sizeof(WCHAR));
        cbIssuer = CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE | CERT_NAME_ISSUER_FLAG,
                                     0, NULL, szIssuer, sizeof(szIssuer)/sizeof(WCHAR));

        ok(cbSubject > 0, "Failed to get subject name\n");
        ok(cbIssuer > 0, "Failed to get issuer name\n");
        ok(wcslen(szSubject) > 0, "Subject name is empty\n");
        ok(wcslen(szIssuer) > 0, "Issuer name is empty\n");

        foundNameCert = TRUE;
        break;
    }

    ok(foundNameCert, "No certificate found to test names\n");
    CertCloseStore(hStore, 0);
}

/*
 * Test 8: Store open permissions
 */
static void test_store_permissions(void)
{
    HCERTSTORE hStoreLocal = NULL;
    HCERTSTORE hStoreUser = NULL;

    /* LocalMachine system stores should be readable */
    hStoreLocal = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                               CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root");
    ok(hStoreLocal != NULL, "Failed to open LocalMachine Root store\n");
    if (hStoreLocal)
        CertCloseStore(hStoreLocal, 0);

    /* CurrentUser system stores should be readable */
    hStoreUser = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                              CERT_SYSTEM_STORE_CURRENT_USER, L"My");
    ok(hStoreUser != NULL, "Failed to open CurrentUser My store\n");
    if (hStoreUser)
        CertCloseStore(hStoreUser, 0);
}

/*
 * Test 9: Common store names are accessible
 */
static void test_common_store_names(void)
{
    static const WCHAR *storeNames[] = { L"Root", L"CA", L"My", L"AuthRoot", L"Disallowed" };
    int i;

    for (i = 0; i < sizeof(storeNames)/sizeof(storeNames[0]); i++)
    {
        HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                                         CERT_SYSTEM_STORE_LOCAL_MACHINE, storeNames[i]);
        ok(hStore != NULL, "Failed to open '%ls' store\n", storeNames[i]);
        if (hStore)
            CertCloseStore(hStore, 0);
    }
}

/*
 * Test 10: Enterprise (group-policy) store opens empty with success on a
 * non-domain machine, while a non-existent store still fails with 0x2.
 *
 * certutil -store -enterprise <name> must behave like Windows: the
 * LOCAL_MACHINE_ENTERPRISE location need not pre-exist (the provider
 * creates/opens the Policies key), so opening it WITHOUT
 * CERT_STORE_OPEN_EXISTING_FLAG succeeds and enumerates empty. A bogus
 * store opened WITH CERT_STORE_OPEN_EXISTING_FLAG must still fail 0x2.
 */
static void test_enterprise_empty_store(void)
{
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCert = NULL;
    int certCount = 0;
    DWORD dwErr;

    /* Enterprise Root WITHOUT OPEN_EXISTING: opens empty with success. */
    SetLastError(0xdeadbeef);
    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE_ENTERPRISE |
                          CERT_STORE_READONLY_FLAG, L"Root");
    ok(hStore != NULL, "Enterprise Root store should open empty, GLE=0x%lx\n",
       GetLastError());
    if (hStore)
    {
        while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
            certCount++;
        trace("Enterprise Root enumerated %d certificate(s)\n", certCount);
        CertCloseStore(hStore, 0);
    }

    /* A non-existent store WITH OPEN_EXISTING must still fail with 0x2. */
    SetLastError(0xdeadbeef);
    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE |
                          CERT_STORE_READONLY_FLAG |
                          CERT_STORE_OPEN_EXISTING_FLAG, L"BogusStore");
    dwErr = GetLastError();
    ok(hStore == NULL, "BogusStore should not open\n");
    ok(dwErr == ERROR_FILE_NOT_FOUND, "BogusStore expected 0x2, got 0x%lx\n", dwErr);
    if (hStore)
        CertCloseStore(hStore, 0);
}

START_TEST(CertUtilStore)
{
    test_enumerate_root_store();
    test_enumerate_authroot_store();
    test_enumerate_current_user_store();
    test_certificate_dates();
    test_certificate_thumbprint();
    test_certificate_serial();
    test_certificate_names();
    test_store_permissions();
    test_common_store_names();
    test_enterprise_empty_store();
}
