/*
 * PROJECT:     ReactOS certutil
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CertUtil -store verb: enumerate certificate stores
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include "precomp.h"
#include <wincrypt.h>

/*
 * Enumerate certificates in a store and display in Windows format
 */
BOOL enumerate_store(HCERTSTORE hStore, LPCWSTR StoreName, LPCWSTR LocationString)
{
    PCCERT_CONTEXT pCertContext = NULL;
    int certCount = 0;

    while ((pCertContext = CertEnumCertificatesInStore(hStore, pCertContext)) != NULL)
    {
        WCHAR szSubject[256], szIssuer[256];
        BYTE rgbThumbprint[20];
        DWORD cbThumbprint = sizeof(rgbThumbprint);
        SYSTEMTIME stBefore, stAfter;
        PCRYPT_INTEGER_BLOB pSerial = &pCertContext->pCertInfo->SerialNumber;
        WCHAR szDateBefore[64], szTimeBefore[64], szDateAfter[64], szTimeAfter[64];

        /* Get subject and issuer as X500 names */
        CertGetNameStringW(pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL,
                          szSubject, sizeof(szSubject)/sizeof(WCHAR));
        CertGetNameStringW(pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE | CERT_NAME_ISSUER_FLAG, 0, NULL,
                          szIssuer, sizeof(szIssuer)/sizeof(WCHAR));

        /* Get SHA1 thumbprint */
        if (!CertGetCertificateContextProperty(pCertContext, CERT_SHA1_HASH_PROP_ID,
                                              rgbThumbprint, &cbThumbprint))
            cbThumbprint = 0;

        /* Convert dates to local time and format */
        FILETIME ftLocal;
        FileTimeToLocalFileTime(&pCertContext->pCertInfo->NotBefore, &ftLocal);
        FileTimeToSystemTime(&ftLocal, &stBefore);
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &stBefore, NULL,
                      szDateBefore, sizeof(szDateBefore)/sizeof(WCHAR));
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_FORCE24HOURFORMAT, &stBefore, NULL,
                      szTimeBefore, sizeof(szTimeBefore)/sizeof(WCHAR));

        FileTimeToLocalFileTime(&pCertContext->pCertInfo->NotAfter, &ftLocal);
        FileTimeToSystemTime(&ftLocal, &stAfter);
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &stAfter, NULL,
                      szDateAfter, sizeof(szDateAfter)/sizeof(WCHAR));
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_FORCE24HOURFORMAT, &stAfter, NULL,
                      szTimeAfter, sizeof(szTimeAfter)/sizeof(WCHAR));

        /* Print Windows-format certificate info */
        ConPrintf(StdOut, L"================ Certificate %d ================\n", certCount);

        /* Serial number in reverse byte order */
        ConPuts(StdOut, L"Serial Number: ");
        for (DWORD i = pSerial->cbData; i > 0; i--)
            ConPrintf(StdOut, L"%02X", pSerial->pbData[i-1]);
        ConPuts(StdOut, L"\n");

        ConPrintf(StdOut, L"Issuer: %s\n", szIssuer);
        ConPrintf(StdOut, L" NotBefore: %s %s\n", szDateBefore, szTimeBefore);
        ConPrintf(StdOut, L" NotAfter: %s %s\n", szDateAfter, szTimeAfter);
        ConPrintf(StdOut, L"Subject: %s\n", szSubject);

        /* SHA1 thumbprint in lowercase hex */
        ConPuts(StdOut, L"Cert Hash(sha1): ");
        for (DWORD i = 0; i < cbThumbprint; i++)
            ConPrintf(StdOut, L"%02x", rgbThumbprint[i]);
        ConPuts(StdOut, L"\n\n");

        certCount++;
    }

    return TRUE;
}

/*
 * Main -store verb handler
 */
BOOL store_verb(LPCWSTR Argument1, LPCWSTR Argument2)
{
    HCERTSTORE hStore = NULL;
    DWORD dwFlags = CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG;
    LPCWSTR StoreName = Argument1;
    LPCWSTR LocationString = NULL;

    /* Check for -user or -enterprise flags; -enterprise = CERT_SYSTEM_STORE_LOCAL_MACHINE (same as default) */
    if (Argument1)
    {
        if (!_wcsicmp(Argument1, L"-user"))
        {
            dwFlags = CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG;
            StoreName = Argument2;
            LocationString = L"CurrentUser";
        }
        else if (!_wcsicmp(Argument1, L"-enterprise"))
        {
            /* -enterprise is LocalMachine (default), just with explicit flag */
            StoreName = Argument2;
            LocationString = L"LocalMachine";
        }
        else
        {
            LocationString = L"LocalMachine";
        }
    }
    else
    {
        LocationString = L"LocalMachine";
    }

    if (!StoreName || StoreName[0] == L'\0')
    {
        ConPuts(StdOut, L"Usage: certutil -store [-user|-enterprise] StoreName\n");
        ConPuts(StdOut, L"  Common store names: Root, CA, My, AuthRoot, Disallowed\n");
        return FALSE;
    }

    /* Open the certificate store (read-only, must exist) */
    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, NULL, dwFlags, StoreName);
    if (!hStore)
    {
        DWORD dwErr = GetLastError();
        ConPrintf(StdOut, L"CertUtil: -store command FAILED: 0x%lx (%s not found)\n", dwErr, StoreName);
        return FALSE;
    }

    /* Enumerate and display certificates */
    BOOL result = enumerate_store(hStore, StoreName, LocationString);

    /* Close the store */
    CertCloseStore(hStore, 0);

    if (result)
        ConPuts(StdOut, L"CertUtil: -store command completed successfully\n");

    return result;
}
