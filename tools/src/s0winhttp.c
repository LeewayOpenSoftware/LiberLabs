/* SPDX-License-Identifier: GPL-2.0-or-later */
/* S0 diagnostic WinHTTP HTTPS client. Connects to the local S0 server only.
 * argv[1] optional: "ignoreca" -> SECURITY_FLAG_IGNORE_UNKNOWN_CA,
 *                    "ignoreall" -> also IGNORE_CERT_CN_INVALID.
 * Prints the exact failing stage and error. */
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>

#define TARGET (argc > 2 ? argv[2] : "10.0.2.15")
#define PORT (argc > 3 ? atoi(argv[3]) : 4443)

static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    fflush(stdout);
    va_end(ap);
}

static void CALLBACK status_cb(HINTERNET h, DWORD_PTR ctx, DWORD status,
                               LPVOID info, DWORD info_len)
{
    (void)h; (void)ctx;
    if (status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE)
    {
        DWORD err = info ? *(DWORD *)info : 0;
        logf("CB SECURE_FAILURE gle=%lu flags=0x%lx (CN_INVALID=0x1 DATE_INVALID=0x2 INVALID_CA=0x4 INVALID_CERT=0x8 REVOKED=0x10 SERVER_CERT=0x20 CHANNEL=0x40)\n",
             err, info_len);
    }
    else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)
        logf("CB SENDREQUEST_COMPLETE\n");
}

int main(int argc, char **argv)
{
    HINTERNET session = NULL, connect = NULL, request = NULL;
    DWORD flags = 0, err, status = 0, len = sizeof(status);
    BOOL ok;

    WCHAR targetW[128];

    logf("S0WINHTTP start target=%s:%d arg=%s\n", TARGET, PORT, argc > 1 ? argv[1] : "(none)");
    mbstowcs(targetW, TARGET, 127);
    targetW[127] = 0;

    session = WinHttpOpen(L"S0Probe/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        logf("FAIL WinHttpOpen gle=%lu\n", GetLastError());
        return 1;
    }

    if (argc > 1 && !_stricmp(argv[1], "ignoreca"))
        flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    if (argc > 1 && !_stricmp(argv[1], "ignoreall"))
        flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;

    if (flags && !WinHttpSetOption(session, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags)))
        logf("note: WinHttpSetOption(SESSION,SECURITY_FLAGS) failed gle=%lu\n", GetLastError());

    connect = WinHttpConnect(session, targetW, PORT, 0);
    if (!connect)
    {
        logf("FAIL WinHttpConnect gle=%lu\n", GetLastError());
        return 1;
    }

    request = WinHttpOpenRequest(connect, L"GET", L"/", NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
    {
        logf("FAIL WinHttpOpenRequest gle=%lu\n", GetLastError());
        return 1;
    }
    WinHttpSetStatusCallback(request, status_cb,
                             WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0);

    if (flags && !WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags)))
        logf("note: WinHttpSetOption(REQUEST,SECURITY_FLAGS) failed gle=%lu\n", GetLastError());

    ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!ok)
    {
        err = GetLastError();
        logf("FAIL WinHttpSendRequest ok=0 gle=%lu (0x%lx)\n", err, err);
        logf("RESULT 12157-repro=%d\n", err == ERROR_WINHTTP_SECURE_CHANNEL_ERROR);
        return 2;
    }
    ok = WinHttpReceiveResponse(request, NULL);
    if (!ok)
    {
        err = GetLastError();
        logf("FAIL WinHttpReceiveResponse ok=0 gle=%lu (0x%lx)\n", err, err);
        logf("RESULT 12157-repro=%d\n", err == ERROR_WINHTTP_SECURE_CHANNEL_ERROR);
        return 3;
    }
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX))
        logf("SUCCESS http status=%lu\n", status);
    else
        logf("SUCCESS (status query failed gle=%lu)\n", GetLastError());
    return 0;
}
