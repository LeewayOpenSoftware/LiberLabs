/*
 * PROJECT:     LiberNT Network and Sharing Center
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Network state and diagnostics backend, no user interface
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Every function reads the real state through documented user-mode APIs. A value that cannot be
 * read is reported as NC_UNAVAILABLE or NC_UNSUPPORTED, never guessed.
 */

#ifndef NETCENTER_H
#define NETCENTER_H

#include <windef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NC_MAX_ADDRESSES 8
#define NC_MAX_SERVERS   6
#define NC_TEXT          64

/* State of a value or feature */
typedef enum _NC_STATE
{
    NC_UNAVAILABLE = 0, /* the query failed or returned nothing */
    NC_UNSUPPORTED,     /* this system has no component for it */
    NC_OFF,
    NC_ON
} NC_STATE;

/* Reachability, always the result of a check, with the evidence in NC_ACCESS.Detail */
typedef enum _NC_LEVEL
{
    NC_LEVEL_UNKNOWN = 0, /* not checked yet or nothing to check */
    NC_LEVEL_NONE,        /* no usable configuration on the link */
    NC_LEVEL_LOCAL,       /* configured, no default gateway or the Internet probe failed */
    NC_LEVEL_INTERNET     /* a connection to the probe host was made */
} NC_LEVEL;

/* Link state of an adapter, from the operational status and the media connect state of the stack */
typedef enum _NC_LINK
{
    NC_LINK_UNKNOWN = 0,
    NC_LINK_CONNECTED,   /* operational */
    NC_LINK_UNPLUGGED,   /* not operational and the media reports no link (cable unplugged) */
    NC_LINK_DOWN,        /* not operational, no media information: disabled or failed */
    NC_LINK_NOT_PRESENT, /* the hardware is gone */
    NC_LINK_LOWER_DOWN   /* an interface below this one is down */
} NC_LINK;

typedef struct _NC_IPADDR
{
    WCHAR Text[NC_TEXT];
    ULONG PrefixLength;
    BOOL IsV6;
    BOOL IsLinkLocal;
} NC_IPADDR;

typedef struct _NC_ADAPTER
{
    WCHAR Name[NC_TEXT * 2];        /* friendly name */
    WCHAR Description[NC_TEXT * 2];
    WCHAR Guid[NC_TEXT];            /* adapter name as the stack reports it */
    BYTE Mac[8];
    ULONG MacLength;
    ULONG IfType;                   /* IF_TYPE_* */
    ULONG IfIndex;
    ULONG Mtu;
    ULONG64 TransmitLinkSpeed;      /* bits per second, 0 when unknown */
    ULONG64 ReceiveLinkSpeed;
    BOOL IsUp;                      /* IfOperStatusUp */
    NC_LINK Link;
    BOOL IsLoopback;
    NC_STATE Dhcp4;                 /* NC_UNAVAILABLE when the stack does not say */
    NC_STATE Dhcp6;
    ULONG AddressCount;
    NC_IPADDR Addresses[NC_MAX_ADDRESSES];
    ULONG GatewayCount;
    WCHAR Gateways[NC_MAX_SERVERS][NC_TEXT];
    ULONG DnsCount;
    WCHAR DnsServers[NC_MAX_SERVERS][NC_TEXT];
    WCHAR DhcpServer[NC_TEXT];      /* empty when none is known */
    BOOL HasStatistics;             /* from GetIfEntry2; the next four are valid only then */
    ULONG64 InOctets;
    ULONG64 OutOctets;
    ULONG64 InErrors;
    ULONG64 OutErrors;
    BOOL HasGlobalV4;               /* a non link-local IPv4 address */
    BOOL HasGlobalV6;               /* a non link-local, non loopback IPv6 address */
    BOOL HasGatewayV4;
    BOOL HasGatewayV6;
} NC_ADAPTER;

typedef struct _NC_ADAPTER_LIST
{
    ULONG Count;
    NC_ADAPTER Adapters[ANYSIZE_ARRAY];
} NC_ADAPTER_LIST;

typedef struct _NC_IDENTITY
{
    WCHAR ComputerName[64];
    WCHAR Domain[128];   /* the workgroup or domain name, empty when not available */
    BOOL IsDomain;
} NC_IDENTITY;

/* Result of the Internet probe */
typedef struct _NC_ACCESS
{
    NC_LEVEL V4;
    NC_LEVEL V6;
    WCHAR Detail[256];   /* what was tried and what happened, shown to the user */
} NC_ACCESS;

typedef struct _NC_FEATURES
{
    NC_STATE NetworkDiscovery;
    NC_STATE FileSharing;
    NC_STATE PrinterSharing;
} NC_FEATURES;

/* Tools the center links to */
typedef enum _NC_TOOL
{
    NC_TOOL_CONNECTIONS = 0, /* Network Connections folder */
    NC_TOOL_INTERNET_OPTIONS,
    NC_TOOL_FIREWALL,
    NC_TOOL_IPCONFIG,        /* ipconfig /all in a console window */
    NC_TOOL_COUNT
} NC_TOOL;

/* One diagnostic step */
typedef enum _NC_DIAG_RESULT
{
    NC_DIAG_PASS = 0,
    NC_DIAG_FAIL,
    NC_DIAG_WARN,        /* inconclusive: the remote end may simply not answer */
    NC_DIAG_UNSUPPORTED, /* the API is not implemented on this system */
    NC_DIAG_SKIPPED      /* an earlier step makes this one meaningless */
} NC_DIAG_RESULT;

typedef struct _NC_DIAG_STEP
{
    WCHAR Title[NC_TEXT];
    NC_DIAG_RESULT Result;
    WCHAR Detail[256];
} NC_DIAG_STEP;

#define NC_DIAG_STEP_COUNT 7

typedef struct _NC_DIAG_REPORT
{
    NC_DIAG_STEP Steps[NC_DIAG_STEP_COUNT];
    ULONG Done;          /* steps filled so far */
} NC_DIAG_REPORT;

/* Called after each step with the report so far; may be NULL. Runs on the calling thread. */
typedef VOID (CALLBACK *NC_DIAG_PROGRESS)(const NC_DIAG_REPORT *Report, PVOID Context);

/* Adapters. Returns NULL and sets *Error when the stack cannot be queried. Free with NcFreeAdapterList. */
NC_ADAPTER_LIST *NcEnumerateAdapters(ULONG *Error);
VOID NcFreeAdapterList(NC_ADAPTER_LIST *List);

BOOL NcGetIdentity(NC_IDENTITY *Identity);

/* Reachability of the Internet from this computer: blocks for a few seconds, call from a worker thread.
 * Configuration (an address, a gateway) is not access: only a connection to the probe counts. */
VOID NcProbeInternet(const NC_ADAPTER_LIST *List, NC_ACCESS *Access);

/* A hash of what decides reachability (connected adapters, their addresses and gateways): two lists
 * with different signatures need a new Internet check. */
ULONG NcAdapterSignature(const NC_ADAPTER_LIST *List);

/* Services behind the sharing and discovery settings */
VOID NcQueryFeatures(NC_FEATURES *Features);

/* Whether the tool exists on this system (its file is installed) */
BOOL NcToolAvailable(NC_TOOL Tool);
/* Starts the tool; returns FALSE when it is missing or the launch failed */
BOOL NcLaunchTool(HWND Owner, NC_TOOL Tool);

/* Runs all diagnostic steps in order and fills the report; blocks, call from a worker thread */
VOID NcRunDiagnostics(NC_DIAG_REPORT *Report, NC_DIAG_PROGRESS Progress, PVOID Context);

/* Plain text renderings used by the UI */
VOID NcFormatMac(const NC_ADAPTER *Adapter, WCHAR *Text, UINT Count);
VOID NcFormatSpeed(ULONG64 BitsPerSecond, WCHAR *Text, UINT Count);
PCWSTR NcDiagResultText(NC_DIAG_RESULT Result);
PCWSTR NcLevelText(NC_LEVEL Level);
PCWSTR NcLinkText(NC_LINK Link);

#ifdef __cplusplus
}
#endif

#endif
