/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for WSAStringToAddress with addresses that are not byte-order symmetric
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include "ws2_32.h"

static
void
TestIPv4(
    PSTR String,
    ULONG ExpectedAddress,
    USHORT ExpectedPort)
{
    SOCKADDR_IN Address;
    INT Length = sizeof(Address);
    INT Result;

    memset(&Address, 0xCC, sizeof(Address));
    Result = WSAStringToAddressA(String, AF_INET, NULL, (PSOCKADDR)&Address, &Length);
    ok(Result == 0, "'%s': result %d, error %d\n", String, Result, WSAGetLastError());
    if (Result != 0)
        return;

    ok(Length == (INT)sizeof(Address), "'%s': length %d\n", String, Length);
    ok(Address.sin_family == AF_INET, "'%s': family %d\n", String, Address.sin_family);
    ok(Address.sin_addr.s_addr == htonl(ExpectedAddress),
       "'%s': address 0x%08lx, expected 0x%08lx in network order\n",
       String, Address.sin_addr.s_addr, htonl(ExpectedAddress));
    ok(Address.sin_port == htons(ExpectedPort),
       "'%s': port 0x%04x, expected 0x%04x in network order\n",
       String, Address.sin_port, htons(ExpectedPort));
}

static
void
TestIPv4Invalid(
    PSTR String)
{
    SOCKADDR_IN Address;
    INT Length = sizeof(Address);
    INT Result;

    Result = WSAStringToAddressA(String, AF_INET, NULL, (PSOCKADDR)&Address, &Length);
    ok(Result == SOCKET_ERROR, "'%s': result %d\n", String, Result);
    ok(WSAGetLastError() == WSAEINVAL, "'%s': error %d\n", String, WSAGetLastError());
}

START_TEST(WSAStringToAddress)
{
    WSADATA WsaData;
    SOCKADDR_IN Address;
    CHAR Text[64];
    DWORD TextLength = sizeof(Text);
    INT Length = sizeof(Address);
    INT Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup returned %d\n", Result);
    if (Result != 0)
        return;

    TestIPv4("1.2.3.4", 0x01020304, 0);
    TestIPv4("127.0.0.1", 0x7F000001, 0);
    TestIPv4("10.0.2.2", 0x0A000202, 0);
    TestIPv4("1.2.3.4:80", 0x01020304, 80);
    TestIPv4("192.168.1.100:65535", 0xC0A80164, 65535);
    TestIPv4("10.0.2.2:443", 0x0A000202, 443);

    TestIPv4Invalid("1.2.3.4:70000");
    TestIPv4Invalid("1.2.3.4:");
    TestIPv4Invalid("1.2.3");
    TestIPv4Invalid("1.2.3.256");

    /* The round trip back to text has to give the original string */
    memset(&Address, 0, sizeof(Address));
    Result = WSAStringToAddressA("172.16.254.1:8080", AF_INET, NULL, (PSOCKADDR)&Address, &Length);
    ok(Result == 0, "result %d, error %d\n", Result, WSAGetLastError());
    if (Result == 0)
    {
        Result = WSAAddressToStringA((PSOCKADDR)&Address, sizeof(Address), NULL, Text, &TextLength);
        ok(Result == 0, "WSAAddressToStringA: result %d, error %d\n", Result, WSAGetLastError());
        if (Result == 0)
            ok(strcmp(Text, "172.16.254.1:8080") == 0, "round trip gave '%s'\n", Text);
    }

    WSACleanup();
}
