/*
 * PROJECT:     LiberNT Network and Sharing Center
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Network state and diagnostics backend, no user interface
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <stdio.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winsvc.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <strsafe.h>
#include <lm.h>

#include "netcenter.h"

#define CONNECT_TIMEOUT_MS 3000
#define ICMP_TIMEOUT_MS    1500
#define DEFAULT_PROBE_V4   L"1.1.1.1"
#define DEFAULT_PROBE_V6   L"2606:4700:4700::1111"
#define DEFAULT_PROBE_PORT 443
#define DEFAULT_PROBE_NAME L"example.com"
#define CONFIG_KEY         L"SOFTWARE\\Leeway\\NetworkCenter"

/* ---- helpers ---- */

static VOID Reset(WCHAR *Text, UINT Count)
{
    if (Count)
        Text[0] = UNICODE_NULL;
}

static VOID ReadConfigText(PCWSTR Name, PCWSTR Default, WCHAR *Text, DWORD Count)
{
    HKEY key;
    DWORD type, size = Count * sizeof(WCHAR);

    StringCchCopyW(Text, Count, Default);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, CONFIG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(key, Name, NULL, &type, (LPBYTE)Text, &size) != ERROR_SUCCESS || type != REG_SZ || !Text[0])
        StringCchCopyW(Text, Count, Default);
    Text[Count - 1] = UNICODE_NULL;
    RegCloseKey(key);
}

static DWORD ReadConfigPort(VOID)
{
    HKEY key;
    DWORD type, value = DEFAULT_PROBE_PORT, size = sizeof(value);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, CONFIG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(key, L"ProbePort", NULL, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS ||
            type != REG_DWORD || value == 0 || value > 65535)
            value = DEFAULT_PROBE_PORT;
        RegCloseKey(key);
    }
    return value;
}

static VOID FormatSockaddr(const SOCKADDR *Address, ULONG Length, WCHAR *Text, UINT Count)
{
    DWORD size = Count;

    Reset(Text, Count);
    if (WSAAddressToStringW((LPSOCKADDR)Address, Length, NULL, Text, &size) != 0)
        Reset(Text, Count);
}

static BOOL IsLinkLocalV4(const SOCKADDR_IN *Address)
{
    return (Address->sin_addr.S_un.S_un_b.s_b1 == 169 && Address->sin_addr.S_un.S_un_b.s_b2 == 254);
}

static BOOL IsLinkLocalV6(const SOCKADDR_IN6 *Address)
{
    return (Address->sin6_addr.u.Byte[0] == 0xFE && (Address->sin6_addr.u.Byte[1] & 0xC0) == 0x80);
}

static BOOL IsLoopbackV6(const SOCKADDR_IN6 *Address)
{
    ULONG i;

    for (i = 0; i < 15; i++)
        if (Address->sin6_addr.u.Byte[i])
            return FALSE;
    return Address->sin6_addr.u.Byte[15] == 1;
}

/* ---- adapters ---- */

static VOID CopyAdapter(NC_ADAPTER *Out, const IP_ADAPTER_ADDRESSES *In)
{
    const IP_ADAPTER_UNICAST_ADDRESS *unicast;
    const IP_ADAPTER_DNS_SERVER_ADDRESS *dns;
    const IP_ADAPTER_GATEWAY_ADDRESS_LH *gateway;
    MIB_IF_ROW2 row;
    ULONG index;
    WCHAR name[NC_TEXT];

    RtlZeroMemory(Out, sizeof(*Out));
    if (In->FriendlyName)
        StringCchCopyW(Out->Name, ARRAYSIZE(Out->Name), In->FriendlyName);
    if (In->Description)
        StringCchCopyW(Out->Description, ARRAYSIZE(Out->Description), In->Description);
    if (In->AdapterName)
    {
        MultiByteToWideChar(CP_ACP, 0, In->AdapterName, -1, name, ARRAYSIZE(name));
        StringCchCopyW(Out->Guid, ARRAYSIZE(Out->Guid), name);
    }
    Out->MacLength = min(In->PhysicalAddressLength, sizeof(Out->Mac));
    RtlCopyMemory(Out->Mac, In->PhysicalAddress, Out->MacLength);
    Out->IfType = In->IfType;
    Out->IfIndex = In->IfIndex ? In->IfIndex : In->Ipv6IfIndex;
    Out->Mtu = In->Mtu;
    Out->TransmitLinkSpeed = (In->TransmitLinkSpeed == (ULONG64)-1) ? 0 : In->TransmitLinkSpeed;
    Out->ReceiveLinkSpeed = (In->ReceiveLinkSpeed == (ULONG64)-1) ? 0 : In->ReceiveLinkSpeed;
    Out->IsUp = (In->OperStatus == IfOperStatusUp);
    Out->IsLoopback = (In->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
    Out->Dhcp4 = (In->Flags & IP_ADAPTER_DHCP_ENABLED) ? NC_ON : NC_OFF;
    Out->Dhcp6 = NC_UNAVAILABLE;
    if (In->Dhcpv4Server.lpSockaddr)
        FormatSockaddr(In->Dhcpv4Server.lpSockaddr, In->Dhcpv4Server.iSockaddrLength, Out->DhcpServer, ARRAYSIZE(Out->DhcpServer));

    for (unicast = In->FirstUnicastAddress; unicast && Out->AddressCount < NC_MAX_ADDRESSES; unicast = unicast->Next)
    {
        NC_IPADDR *entry = &Out->Addresses[Out->AddressCount];
        const SOCKADDR *sa = unicast->Address.lpSockaddr;

        if (!sa)
            continue;
        if (sa->sa_family == AF_INET)
        {
            entry->IsV6 = FALSE;
            entry->IsLinkLocal = IsLinkLocalV4((const SOCKADDR_IN *)sa);
            if (!entry->IsLinkLocal)
                Out->HasGlobalV4 = TRUE;
        }
        else if (sa->sa_family == AF_INET6)
        {
            entry->IsV6 = TRUE;
            entry->IsLinkLocal = IsLinkLocalV6((const SOCKADDR_IN6 *)sa);
            if (!entry->IsLinkLocal && !IsLoopbackV6((const SOCKADDR_IN6 *)sa))
                Out->HasGlobalV6 = TRUE;
        }
        else
            continue;
        entry->PrefixLength = unicast->OnLinkPrefixLength;
        FormatSockaddr(sa, unicast->Address.iSockaddrLength, entry->Text, ARRAYSIZE(entry->Text));
        if (entry->Text[0])
            Out->AddressCount++;
    }
    for (gateway = In->FirstGatewayAddress; gateway && Out->GatewayCount < NC_MAX_SERVERS; gateway = gateway->Next)
    {
        const SOCKADDR *sa = gateway->Address.lpSockaddr;

        if (!sa)
            continue;
        FormatSockaddr(sa, gateway->Address.iSockaddrLength, Out->Gateways[Out->GatewayCount], NC_TEXT);
        if (!Out->Gateways[Out->GatewayCount][0])
            continue;
        if (sa->sa_family == AF_INET)
            Out->HasGatewayV4 = TRUE;
        else if (sa->sa_family == AF_INET6)
            Out->HasGatewayV6 = TRUE;
        Out->GatewayCount++;
    }
    for (dns = In->FirstDnsServerAddress; dns && Out->DnsCount < NC_MAX_SERVERS; dns = dns->Next)
    {
        if (!dns->Address.lpSockaddr)
            continue;
        FormatSockaddr(dns->Address.lpSockaddr, dns->Address.iSockaddrLength, Out->DnsServers[Out->DnsCount], NC_TEXT);
        if (Out->DnsServers[Out->DnsCount][0])
            Out->DnsCount++;
    }

    index = Out->IfIndex;
    RtlZeroMemory(&row, sizeof(row));
    row.InterfaceIndex = index;
    if (index && GetIfEntry2(&row) == NO_ERROR)
    {
        Out->HasStatistics = TRUE;
        Out->InOctets = row.InOctets;
        Out->OutOctets = row.OutOctets;
        Out->InErrors = row.InErrors;
        Out->OutErrors = row.OutErrors;
    }
}

NC_ADAPTER_LIST *NcEnumerateAdapters(ULONG *Error)
{
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
    ULONG size = 16 * 1024, result, count, i;
    IP_ADAPTER_ADDRESSES *buffer = NULL, *adapter;
    NC_ADAPTER_LIST *list;
    ULONG attempt;

    for (attempt = 0; attempt < 4; attempt++)
    {
        buffer = HeapAlloc(GetProcessHeap(), 0, size);
        if (!buffer)
        {
            *Error = ERROR_NOT_ENOUGH_MEMORY;
            return NULL;
        }
        result = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, buffer, &size);
        if (result != ERROR_BUFFER_OVERFLOW)
            break;
        HeapFree(GetProcessHeap(), 0, buffer);
        buffer = NULL;
    }
    if (result != NO_ERROR)
    {
        if (buffer)
            HeapFree(GetProcessHeap(), 0, buffer);
        *Error = (result == ERROR_NO_DATA) ? NO_ERROR : result;
        if (result != ERROR_NO_DATA)
            return NULL;
        buffer = NULL;
    }

    count = 0;
    for (adapter = buffer; adapter; adapter = adapter->Next)
        count++;
    list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, FIELD_OFFSET(NC_ADAPTER_LIST, Adapters) + max(count, 1) * sizeof(NC_ADAPTER));
    if (!list)
    {
        if (buffer)
            HeapFree(GetProcessHeap(), 0, buffer);
        *Error = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    for (adapter = buffer, i = 0; adapter; adapter = adapter->Next, i++)
        CopyAdapter(&list->Adapters[i], adapter);
    list->Count = count;
    if (buffer)
        HeapFree(GetProcessHeap(), 0, buffer);
    *Error = NO_ERROR;
    return list;
}

VOID NcFreeAdapterList(NC_ADAPTER_LIST *List)
{
    if (List)
        HeapFree(GetProcessHeap(), 0, List);
}

BOOL NcGetIdentity(NC_IDENTITY *Identity)
{
    DWORD size = ARRAYSIZE(Identity->ComputerName);
    LPWSTR name = NULL;
    NETSETUP_JOIN_STATUS status;

    RtlZeroMemory(Identity, sizeof(*Identity));
    if (!GetComputerNameW(Identity->ComputerName, &size))
        return FALSE;
    if (NetGetJoinInformation(NULL, &name, &status) == NERR_Success)
    {
        if (name && (status == NetSetupWorkgroupName || status == NetSetupDomainName))
        {
            StringCchCopyW(Identity->Domain, ARRAYSIZE(Identity->Domain), name);
            Identity->IsDomain = (status == NetSetupDomainName);
        }
    }
    if (name)
        NetApiBufferFree(name);
    return TRUE;
}

/* ---- sockets ---- */

static BOOL WinsockUp(VOID)
{
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

/* Makes a TCP connection to the numeric address; returns 0 on success or a Winsock error code */
static int TryConnect(PCWSTR Numeric, USHORT Port)
{
    SOCKADDR_STORAGE storage;
    INT length = sizeof(storage);
    SOCKET sock;
    u_long nonblocking = 1;
    fd_set writeSet, errorSet;
    struct timeval timeout;
    int result, error = 0, errorLength = sizeof(error);

    RtlZeroMemory(&storage, sizeof(storage));
    if (WSAStringToAddressW((LPWSTR)Numeric, AF_INET, NULL, (LPSOCKADDR)&storage, &length) != 0)
    {
        length = sizeof(storage);
        if (WSAStringToAddressW((LPWSTR)Numeric, AF_INET6, NULL, (LPSOCKADDR)&storage, &length) != 0)
            return WSAEINVAL;
    }
    if (storage.ss_family == AF_INET)
        ((SOCKADDR_IN *)&storage)->sin_port = htons(Port);
    else
        ((SOCKADDR_IN6 *)&storage)->sin6_port = htons(Port);

    sock = socket(storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
        return WSAGetLastError();
    ioctlsocket(sock, FIONBIO, &nonblocking);
    result = connect(sock, (SOCKADDR *)&storage, length);
    if (result == SOCKET_ERROR)
    {
        error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
        {
            FD_ZERO(&writeSet);
            FD_ZERO(&errorSet);
            FD_SET(sock, &writeSet);
            FD_SET(sock, &errorSet);
            timeout.tv_sec = CONNECT_TIMEOUT_MS / 1000;
            timeout.tv_usec = 0;
            result = select(0, NULL, &writeSet, &errorSet, &timeout);
            if (result == 0)
                error = WSAETIMEDOUT;
            else if (result == SOCKET_ERROR)
                error = WSAGetLastError();
            else if (FD_ISSET(sock, &writeSet))
            {
                error = 0;
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&error, &errorLength);
            }
            else
            {
                error = 0;
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&error, &errorLength);
                if (!error)
                    error = WSAECONNREFUSED;
            }
        }
    }
    else
        error = 0;
    closesocket(sock);
    return error;
}

/* TCP connection from this computer to itself: listen on 127.0.0.1, connect, accept */
static int LoopbackSelfTest(PCWSTR *Step)
{
    SOCKET listener = INVALID_SOCKET, client = INVALID_SOCKET, server = INVALID_SOCKET;
    SOCKADDR_IN address;
    int length = sizeof(address), error = 0;
    fd_set readSet, writeSet;
    struct timeval timeout;
    u_long nonblocking = 1;

    *Step = L"socket";
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
        return WSAGetLastError();
    RtlZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
    *Step = L"bind";
    if (bind(listener, (SOCKADDR *)&address, sizeof(address)) != 0 || getsockname(listener, (SOCKADDR *)&address, &length) != 0)
    {
        error = WSAGetLastError();
        goto Done;
    }
    *Step = L"listen";
    if (listen(listener, 1) != 0)
    {
        error = WSAGetLastError();
        goto Done;
    }
    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET)
    {
        *Step = L"socket";
        error = WSAGetLastError();
        goto Done;
    }
    ioctlsocket(client, FIONBIO, &nonblocking);
    *Step = L"connect";
    if (connect(client, (SOCKADDR *)&address, sizeof(address)) != 0)
    {
        error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
            goto Done;
        error = 0;
    }
    *Step = L"accept";
    FD_ZERO(&readSet);
    FD_SET(listener, &readSet);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (select(0, &readSet, NULL, NULL, &timeout) <= 0)
    {
        error = WSAETIMEDOUT;
        goto Done;
    }
    server = accept(listener, NULL, NULL);
    if (server == INVALID_SOCKET)
    {
        error = WSAGetLastError();
        goto Done;
    }
    *Step = L"connect completion";
    FD_ZERO(&writeSet);
    FD_SET(client, &writeSet);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (select(0, NULL, &writeSet, NULL, &timeout) <= 0)
        error = WSAETIMEDOUT;
    else
    {
        int soError = 0, soLength = sizeof(soError);
        getsockopt(client, SOL_SOCKET, SO_ERROR, (char *)&soError, &soLength);
        error = soError;
    }
Done:
    if (server != INVALID_SOCKET)
        closesocket(server);
    if (client != INVALID_SOCKET)
        closesocket(client);
    closesocket(listener);
    return error;
}

/* ICMP echo to an IPv4 address; returns 0 with the round trip time, else a Win32 error / IP status */
static DWORD PingV4(PCWSTR Numeric, DWORD *RoundTrip)
{
    SOCKADDR_IN address;
    INT length = sizeof(address);
    HANDLE icmp;
    BYTE data[4] = { 'n', 'c', 's', 'c' };
    BYTE reply[sizeof(ICMP_ECHO_REPLY) + sizeof(data) + 8 + 64];
    DWORD count, status;

    RtlZeroMemory(&address, sizeof(address));
    if (WSAStringToAddressW((LPWSTR)Numeric, AF_INET, NULL, (LPSOCKADDR)&address, &length) != 0)
        return ERROR_INVALID_PARAMETER;
    icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE)
        return GetLastError();
    count = IcmpSendEcho(icmp, address.sin_addr.S_un.S_addr, data, sizeof(data), NULL, reply, sizeof(reply), ICMP_TIMEOUT_MS);
    if (count == 0)
        status = GetLastError();
    else
    {
        ICMP_ECHO_REPLY *echo = (ICMP_ECHO_REPLY *)reply;

        status = (echo->Status == IP_SUCCESS) ? 0 : echo->Status;
        if (!status && RoundTrip)
            *RoundTrip = echo->RoundTripTime;
    }
    IcmpCloseHandle(icmp);
    return status;
}

/* ---- Internet probe ---- */

static BOOL AnyUp(const NC_ADAPTER_LIST *List, BOOL V6, BOOL Gateway)
{
    ULONG i;

    for (i = 0; List && i < List->Count; i++)
    {
        const NC_ADAPTER *a = &List->Adapters[i];

        if (!a->IsUp || a->IsLoopback)
            continue;
        if (V6 ? (Gateway ? a->HasGatewayV6 : a->HasGlobalV6) : (Gateway ? a->HasGatewayV4 : a->HasGlobalV4))
            return TRUE;
    }
    return FALSE;
}

VOID NcProbeInternet(const NC_ADAPTER_LIST *List, NC_ACCESS *Access)
{
    WCHAR probe4[NC_TEXT], probe6[NC_TEXT], line[96];
    USHORT port = (USHORT)ReadConfigPort();
    BOOL winsock = FALSE;
    int error;

    RtlZeroMemory(Access, sizeof(*Access));
    ReadConfigText(L"ProbeV4", DEFAULT_PROBE_V4, probe4, ARRAYSIZE(probe4));
    ReadConfigText(L"ProbeV6", DEFAULT_PROBE_V6, probe6, ARRAYSIZE(probe6));

    if (!AnyUp(List, FALSE, FALSE))
    {
        Access->V4 = NC_LEVEL_NONE;
        StringCchCatW(Access->Detail, ARRAYSIZE(Access->Detail), L"IPv4: no usable address. ");
    }
    else if (!AnyUp(List, FALSE, TRUE))
    {
        Access->V4 = NC_LEVEL_LOCAL;
        StringCchCatW(Access->Detail, ARRAYSIZE(Access->Detail), L"IPv4: no default gateway. ");
    }
    else
    {
        winsock = WinsockUp();
        if (!winsock)
            StringCchCatW(Access->Detail, ARRAYSIZE(Access->Detail), L"IPv4: Winsock is not available. ");
        else
        {
            error = TryConnect(probe4, port);
            Access->V4 = error ? NC_LEVEL_LOCAL : NC_LEVEL_INTERNET;
            if (error)
                StringCchPrintfW(line, ARRAYSIZE(line), L"IPv4: connection to %s:%u failed (error %d). ", probe4, port, error);
            else
                StringCchPrintfW(line, ARRAYSIZE(line), L"IPv4: connected to %s:%u. ", probe4, port);
            StringCchCatW(Access->Detail, ARRAYSIZE(Access->Detail), line);
        }
    }

    if (!AnyUp(List, TRUE, FALSE))
    {
        Access->V6 = NC_LEVEL_NONE;
        StringCchCatW(Access->Detail, ARRAYSIZE(Access->Detail), L"IPv6: no global address.");
    }
    else if (!AnyUp(List, TRUE, TRUE))
    {
        Access->V6 = NC_LEVEL_LOCAL;
        StringCchCatW(Access->Detail, ARRAYSIZE(Access->Detail), L"IPv6: no default gateway.");
    }
    else
    {
        if (!winsock)
            winsock = WinsockUp();
        if (!winsock)
            StringCchCatW(Access->Detail, ARRAYSIZE(Access->Detail), L"IPv6: Winsock is not available.");
        else
        {
            error = TryConnect(probe6, port);
            Access->V6 = error ? NC_LEVEL_LOCAL : NC_LEVEL_INTERNET;
            if (error)
                StringCchPrintfW(line, ARRAYSIZE(line), L"IPv6: connection to [%s]:%u failed (error %d).", probe6, port, error);
            else
                StringCchPrintfW(line, ARRAYSIZE(line), L"IPv6: connected to [%s]:%u.", probe6, port);
            StringCchCatW(Access->Detail, ARRAYSIZE(Access->Detail), line);
        }
    }
    if (winsock)
        WSACleanup();
}

/* ---- sharing and discovery ---- */

static NC_STATE ServiceState(SC_HANDLE Manager, PCWSTR Name)
{
    SC_HANDLE service;
    SERVICE_STATUS status;
    NC_STATE state = NC_UNAVAILABLE;

    service = OpenServiceW(Manager, Name, SERVICE_QUERY_STATUS);
    if (!service)
        return (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) ? NC_UNSUPPORTED : NC_UNAVAILABLE;
    if (QueryServiceStatus(service, &status))
        state = (status.dwCurrentState == SERVICE_RUNNING) ? NC_ON : NC_OFF;
    CloseServiceHandle(service);
    return state;
}

/* Combine services that must all run: unsupported if one is missing, unavailable if one cannot be read */
static NC_STATE AllOf(SC_HANDLE Manager, const PCWSTR *Names, ULONG Count)
{
    NC_STATE result = NC_ON;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        NC_STATE s = ServiceState(Manager, Names[i]);

        if (s == NC_UNSUPPORTED)
            return NC_UNSUPPORTED;
        if (s == NC_UNAVAILABLE)
            result = NC_UNAVAILABLE;
        else if (s == NC_OFF && result == NC_ON)
            result = NC_OFF;
    }
    return result;
}

VOID NcQueryFeatures(NC_FEATURES *Features)
{
    static const PCWSTR discovery[] = { L"FDResPub", L"SSDPSRV" };
    static const PCWSTR files[] = { L"LanmanServer" };
    static const PCWSTR printers[] = { L"LanmanServer", L"Spooler" };
    SC_HANDLE manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);

    if (!manager)
    {
        Features->NetworkDiscovery = Features->FileSharing = Features->PrinterSharing = NC_UNAVAILABLE;
        return;
    }
    Features->NetworkDiscovery = AllOf(manager, discovery, ARRAYSIZE(discovery));
    Features->FileSharing = AllOf(manager, files, ARRAYSIZE(files));
    Features->PrinterSharing = AllOf(manager, printers, ARRAYSIZE(printers));
    CloseServiceHandle(manager);
}

/* ---- tools ---- */

static PCWSTR ToolFile(NC_TOOL Tool)
{
    switch (Tool)
    {
        case NC_TOOL_CONNECTIONS: return L"ncpa.cpl";
        case NC_TOOL_INTERNET_OPTIONS: return L"inetcpl.cpl";
        case NC_TOOL_FIREWALL: return L"firewall.cpl";
        default: return NULL;
    }
}

BOOL NcToolAvailable(NC_TOOL Tool)
{
    WCHAR path[MAX_PATH];
    PCWSTR file = ToolFile(Tool);
    UINT length;

    if (!file)
        return FALSE;
    length = GetSystemDirectoryW(path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path))
        return FALSE;
    if (FAILED(StringCchPrintfW(path + length, ARRAYSIZE(path) - length, L"\\%s", file)))
        return FALSE;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

BOOL NcLaunchTool(HWND Owner, NC_TOOL Tool)
{
    INT_PTR result;

    if (!NcToolAvailable(Tool))
        return FALSE;
    if (Tool == NC_TOOL_CONNECTIONS)
    {
        /* the Network Connections folder, the same target ncpa.cpl opens */
        result = (INT_PTR)ShellExecuteW(Owner, L"open", L"explorer.exe",
                                        L"/n,::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{7007ACC7-3202-11D1-AAD2-00805FC1270E}",
                                        NULL, SW_SHOWNORMAL);
    }
    else
        result = (INT_PTR)ShellExecuteW(Owner, L"open", L"control.exe", ToolFile(Tool), NULL, SW_SHOWNORMAL);
    return result > 32;
}

/* ---- diagnostics ---- */

static VOID SetStep(NC_DIAG_REPORT *Report, ULONG Index, PCWSTR Title, NC_DIAG_RESULT Result, PCWSTR Format, ...)
{
    NC_DIAG_STEP *step = &Report->Steps[Index];
    va_list args;

    StringCchCopyW(step->Title, ARRAYSIZE(step->Title), Title);
    step->Result = Result;
    va_start(args, Format);
    StringCchVPrintfW(step->Detail, ARRAYSIZE(step->Detail), Format, args);
    va_end(args);
}

static BOOL IsNotImplemented(DWORD Error)
{
    return Error == ERROR_NOT_SUPPORTED || Error == ERROR_CALL_NOT_IMPLEMENTED || Error == ERROR_PROC_NOT_FOUND;
}

VOID NcRunDiagnostics(NC_DIAG_REPORT *Report, NC_DIAG_PROGRESS Progress, PVOID Context)
{
    NC_ADAPTER_LIST *list;
    ULONG error, i, up = 0, global4 = 0, linklocal4 = 0, dnsCount = 0;
    BOOL interfacesOk, configOk = FALSE;
    MIB_IPSTATS stats;
    WCHAR gateway[NC_TEXT], name[NC_TEXT], probeName[NC_TEXT];
    NC_ACCESS access;
    DWORD status, rtt = 0;
    PCWSTR stage;
    int sockError;
    BOOL winsock;
    ADDRINFOW hints, *found = NULL;
    int dnsError;

    RtlZeroMemory(Report, sizeof(*Report));
#define DONE() do { Report->Done++; if (Progress) Progress(Report, Context); } while (0)

    /* 0 interfaces */
    list = NcEnumerateAdapters(&error);
    if (!list)
        SetStep(Report, 0, L"Network interfaces", NC_DIAG_FAIL, L"The adapter list cannot be read (error %lu).", error);
    else
    {
        for (i = 0; i < list->Count; i++)
        {
            if (list->Adapters[i].IsUp && !list->Adapters[i].IsLoopback)
                up++;
        }
        if (up)
            SetStep(Report, 0, L"Network interfaces", NC_DIAG_PASS, L"%lu of %lu interface(s) are connected.", up, list->Count);
        else
            SetStep(Report, 0, L"Network interfaces", NC_DIAG_FAIL, L"%lu interface(s) found, none is connected.", list->Count);
    }
    interfacesOk = (up != 0);
    DONE();

    /* 1 IP configuration */
    if (!interfacesOk)
        SetStep(Report, 1, L"IP configuration", NC_DIAG_SKIPPED, L"No connected interface.");
    else
    {
        for (i = 0; i < list->Count; i++)
        {
            const NC_ADAPTER *a = &list->Adapters[i];
            ULONG j;

            if (!a->IsUp || a->IsLoopback)
                continue;
            for (j = 0; j < a->AddressCount; j++)
            {
                if (!a->Addresses[j].IsV6)
                {
                    if (a->Addresses[j].IsLinkLocal)
                        linklocal4++;
                    else
                        global4++;
                }
            }
            dnsCount += a->DnsCount;
        }
        configOk = (global4 != 0);
        if (global4)
            SetStep(Report, 1, L"IP configuration", NC_DIAG_PASS, L"%lu IPv4 address(es) configured, %lu DNS server(s).", global4, dnsCount);
        else if (linklocal4)
            SetStep(Report, 1, L"IP configuration", NC_DIAG_WARN, L"Only a self-assigned 169.254.x.x address: no DHCP lease and no static address.");
        else
            SetStep(Report, 1, L"IP configuration", NC_DIAG_FAIL, L"The connected interface has no IPv4 address.");
    }
    DONE();

    /* 2 local TCP/IP stack */
    RtlZeroMemory(&stats, sizeof(stats));
    error = GetIpStatistics(&stats);
    if (error == NO_ERROR)
        SetStep(Report, 2, L"Local TCP/IP stack", NC_DIAG_PASS, L"The stack answers statistics queries (%lu datagrams received).", stats.dwInReceives);
    else if (IsNotImplemented(error))
        SetStep(Report, 2, L"Local TCP/IP stack", NC_DIAG_UNSUPPORTED, L"GetIpStatistics is not implemented (error %lu).", error);
    else
        SetStep(Report, 2, L"Local TCP/IP stack", NC_DIAG_FAIL, L"The stack does not answer statistics queries (error %lu).", error);
    DONE();

    /* 3 loopback: ICMP echo and a TCP connection to this computer */
    {
        DWORD icmpStatus = PingV4(L"127.0.0.1", &rtt);
        WCHAR icmpText[80], tcpText[96];

        if (!icmpStatus)
            StringCchPrintfW(icmpText, ARRAYSIZE(icmpText), L"ICMP echo to 127.0.0.1: reply in %lu ms", rtt);
        else if (IsNotImplemented(icmpStatus))
            StringCchPrintfW(icmpText, ARRAYSIZE(icmpText), L"ICMP echo: not implemented (error %lu)", icmpStatus);
        else
            StringCchPrintfW(icmpText, ARRAYSIZE(icmpText), L"ICMP echo to 127.0.0.1: no reply (code %lu)", icmpStatus);

        winsock = WinsockUp();
        if (!winsock)
        {
            StringCchCopyW(tcpText, ARRAYSIZE(tcpText), L"TCP: Winsock is not available");
            sockError = WSASYSNOTREADY;
        }
        else
        {
            stage = L"";
            sockError = LoopbackSelfTest(&stage);
            if (!sockError)
                StringCchCopyW(tcpText, ARRAYSIZE(tcpText), L"TCP connect and accept on 127.0.0.1 worked");
            else
                StringCchPrintfW(tcpText, ARRAYSIZE(tcpText), L"TCP loopback failed at %s (error %d)", stage, sockError);
            WSACleanup();
        }
        if (!icmpStatus && !sockError)
            SetStep(Report, 3, L"Loopback", NC_DIAG_PASS, L"%s; %s.", icmpText, tcpText);
        else if (IsNotImplemented(icmpStatus) && !sockError)
            SetStep(Report, 3, L"Loopback", NC_DIAG_UNSUPPORTED, L"%s; %s.", icmpText, tcpText);
        else
            SetStep(Report, 3, L"Loopback", NC_DIAG_FAIL, L"%s; %s.", icmpText, tcpText);
    }
    DONE();

    /* 4 default gateway */
    gateway[0] = UNICODE_NULL;
    if (list)
    {
        for (i = 0; i < list->Count && !gateway[0]; i++)
        {
            const NC_ADAPTER *a = &list->Adapters[i];
            ULONG j;

            if (!a->IsUp || a->IsLoopback)
                continue;
            for (j = 0; j < a->GatewayCount && !gateway[0]; j++)
            {
                SOCKADDR_IN probe;
                INT length = sizeof(probe);

                if (WSAStringToAddressW((LPWSTR)a->Gateways[j], AF_INET, NULL, (LPSOCKADDR)&probe, &length) == 0)
                    StringCchCopyW(gateway, ARRAYSIZE(gateway), a->Gateways[j]);
            }
        }
    }
    if (!configOk)
        SetStep(Report, 4, L"Default gateway", NC_DIAG_SKIPPED, L"No usable IPv4 configuration.");
    else if (!gateway[0])
        SetStep(Report, 4, L"Default gateway", NC_DIAG_WARN, L"No IPv4 default gateway is configured: this computer can reach only its own subnet.");
    else
    {
        status = PingV4(gateway, &rtt);
        if (!status)
            SetStep(Report, 4, L"Default gateway", NC_DIAG_PASS, L"%s answered the echo in %lu ms.", gateway, rtt);
        else if (IsNotImplemented(status))
            SetStep(Report, 4, L"Default gateway", NC_DIAG_UNSUPPORTED, L"ICMP echo is not implemented (error %lu).", status);
        else
            SetStep(Report, 4, L"Default gateway", NC_DIAG_WARN, L"%s did not answer the echo (code %lu); a gateway may ignore ping.", gateway, status);
    }
    DONE();

    /* 5 DNS */
    ReadConfigText(L"ProbeDnsName", DEFAULT_PROBE_NAME, probeName, ARRAYSIZE(probeName));
    if (!configOk)
        SetStep(Report, 5, L"DNS name resolution", NC_DIAG_SKIPPED, L"No usable IPv4 configuration.");
    else if (!dnsCount)
        SetStep(Report, 5, L"DNS name resolution", NC_DIAG_FAIL, L"No DNS server is configured on a connected interface.");
    else
    {
        winsock = WinsockUp();
        if (!winsock)
            SetStep(Report, 5, L"DNS name resolution", NC_DIAG_FAIL, L"Winsock is not available.");
        else
        {
            RtlZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            dnsError = GetAddrInfoW(probeName, NULL, &hints, &found);
            if (dnsError == 0 && found && found->ai_addr)
            {
                FormatSockaddr(found->ai_addr, (ULONG)found->ai_addrlen, name, ARRAYSIZE(name));
                SetStep(Report, 5, L"DNS name resolution", NC_DIAG_PASS, L"%s resolved to %s.", probeName, name);
            }
            else if (dnsError == WSAEAFNOSUPPORT || dnsError == WSAEOPNOTSUPP)
                SetStep(Report, 5, L"DNS name resolution", NC_DIAG_UNSUPPORTED, L"Name resolution is not implemented (error %d).", dnsError);
            else
                SetStep(Report, 5, L"DNS name resolution", NC_DIAG_FAIL, L"%s could not be resolved (error %d).", probeName, dnsError);
            if (found)
                FreeAddrInfoW(found);
            WSACleanup();
        }
    }
    DONE();

    /* 6 Internet: a real connection, not just an address */
    if (!interfacesOk)
        SetStep(Report, 6, L"Internet access", NC_DIAG_SKIPPED, L"No connected interface.");
    else
    {
        NcProbeInternet(list, &access);
        if (access.V4 == NC_LEVEL_INTERNET || access.V6 == NC_LEVEL_INTERNET)
            SetStep(Report, 6, L"Internet access", NC_DIAG_PASS, L"%s", access.Detail);
        else
            SetStep(Report, 6, L"Internet access", NC_DIAG_FAIL, L"No connection to the probe could be made. %s", access.Detail);
    }
    DONE();
#undef DONE
    NcFreeAdapterList(list);
}

/* ---- text ---- */

VOID NcFormatMac(const NC_ADAPTER *Adapter, WCHAR *Text, UINT Count)
{
    ULONG i;
    WCHAR part[4];

    Reset(Text, Count);
    if (!Adapter->MacLength)
    {
        StringCchCopyW(Text, Count, L"-");
        return;
    }
    for (i = 0; i < Adapter->MacLength; i++)
    {
        StringCchPrintfW(part, ARRAYSIZE(part), i ? L"-%02X" : L"%02X", Adapter->Mac[i]);
        StringCchCatW(Text, Count, part);
    }
}

VOID NcFormatSpeed(ULONG64 BitsPerSecond, WCHAR *Text, UINT Count)
{
    if (!BitsPerSecond)
        StringCchCopyW(Text, Count, L"-");
    else if (BitsPerSecond >= 1000000000ULL)
        StringCchPrintfW(Text, Count, L"%lu.%lu Gbps", (ULONG)(BitsPerSecond / 1000000000ULL), (ULONG)((BitsPerSecond / 100000000ULL) % 10));
    else if (BitsPerSecond >= 1000000ULL)
        StringCchPrintfW(Text, Count, L"%lu Mbps", (ULONG)(BitsPerSecond / 1000000ULL));
    else
        StringCchPrintfW(Text, Count, L"%lu kbps", (ULONG)(BitsPerSecond / 1000ULL));
}

PCWSTR NcDiagResultText(NC_DIAG_RESULT Result)
{
    switch (Result)
    {
        case NC_DIAG_PASS: return L"OK";
        case NC_DIAG_FAIL: return L"FAILED";
        case NC_DIAG_WARN: return L"INCONCLUSIVE";
        case NC_DIAG_UNSUPPORTED: return L"NOT SUPPORTED";
        default: return L"SKIPPED";
    }
}

PCWSTR NcLevelText(NC_LEVEL Level)
{
    switch (Level)
    {
        case NC_LEVEL_NONE: return L"No network access";
        case NC_LEVEL_LOCAL: return L"Local only";
        case NC_LEVEL_INTERNET: return L"Internet";
        default: return L"Not checked";
    }
}
