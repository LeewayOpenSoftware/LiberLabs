/*
 * PROJECT:     LiberNT Network and Sharing Center
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Main window: shows the state read by the netcenter backend
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <stdio.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <strsafe.h>

#include "netcenter.h"
#include "resource.h"

#define WM_NCSC_PROBED  (WM_APP + 1)  /* lParam: NC_ACCESS allocated with LocalAlloc */
#define WM_NCSC_TEXT    (WM_APP + 2)  /* lParam: WCHAR text allocated with LocalAlloc */
#define WM_NCSC_NETCHANGE (WM_APP + 3) /* an interface or address changed */
#define TIMER_REFRESH   1
#define TIMER_CHANGE    2
#define CHANGE_DELAY_MS 400
#define REFRESH_MS      15000
#define PROBE_EVERY     4             /* every fourth refresh, about once a minute */
#define MAX_LINKS       64
#define LEFT_WIDTH      230
#define MARGIN          24

enum LinkAction
{
    LA_HOME,
    LA_CONNECTIONS,
    LA_DIAGNOSE,
    LA_REFRESH,
    LA_INTERNET_OPTIONS,
    LA_FIREWALL,
    LA_STATUS,      /* arg: adapter index */
    LA_PROPERTIES   /* arg: adapter index */
};

typedef struct _LINK
{
    RECT Rect;
    int Action;
    int Arg;
    BOOL Enabled;
} LINK;

static HINSTANCE g_Instance;
static HWND g_Main;
static HFONT g_FontNormal, g_FontBold, g_FontTitle, g_FontSmall, g_FontLink;
static NC_ADAPTER_LIST *g_List;
static ULONG g_ListError;
static NC_IDENTITY g_Identity;
static NC_FEATURES g_Features;
static NC_ACCESS g_Access;
static BOOL g_ProbeRunning, g_ProbeDone;
static ULONG g_ProbeGen;      /* generation of the latest probe started */
static ULONG g_ProbeSig;      /* adapter signature the shown result was measured on */
static BOOL g_ReprobeNeeded;  /* the network changed while a probe was running */
static HANDLE g_NotifyInterface, g_NotifyAddress;
static SYSTEMTIME g_ProbeTime;
static LINK g_Links[MAX_LINKS];
static int g_LinkCount, g_Hot = -1;
static int g_Scroll, g_ContentHeight;
static UINT g_Ticks;

#define COLOR_TITLE   RGB(0, 51, 153)
#define COLOR_LINK    RGB(0, 102, 204)
#define COLOR_GRAY    RGB(110, 110, 110)
#define COLOR_PANE    RGB(228, 236, 247)
#define COLOR_GOOD    RGB(34, 139, 34)
#define COLOR_WARN    RGB(214, 140, 0)
#define COLOR_BAD     RGB(192, 32, 32)
#define COLOR_UNKNOWN RGB(150, 150, 150)

/* ---- state ---- */

typedef struct _PROBE_JOB
{
    NC_ADAPTER_LIST *List;
    ULONG Gen;
    ULONG Sig;
} PROBE_JOB;

typedef struct _PROBE_RESULT
{
    ULONG Gen;
    ULONG Sig;
    NC_ACCESS Access;
} PROBE_RESULT;

static DWORD WINAPI ProbeThread(LPVOID Param)
{
    PROBE_JOB *job = Param;
    PROBE_RESULT *result = LocalAlloc(LPTR, sizeof(*result));

    if (result)
    {
        result->Gen = job->Gen;
        result->Sig = job->Sig;
        NcProbeInternet(job->List, &result->Access);
        if (!PostMessageW(g_Main, WM_NCSC_PROBED, 0, (LPARAM)result))
            LocalFree(result);
    }
    NcFreeAdapterList(job->List);
    LocalFree(job);
    return 0;
}

static VOID StartProbe(VOID)
{
    NC_ADAPTER_LIST *copy;
    PROBE_JOB *job;
    SIZE_T size;
    HANDLE thread;

    if (!g_List)
        return;
    if (g_ProbeRunning)
    {
        g_ReprobeNeeded = TRUE;     /* started again when the running probe returns */
        return;
    }
    size = FIELD_OFFSET(NC_ADAPTER_LIST, Adapters) + max(g_List->Count, 1) * sizeof(NC_ADAPTER);
    copy = HeapAlloc(GetProcessHeap(), 0, size);
    job = LocalAlloc(LPTR, sizeof(*job));
    if (!copy || !job)
    {
        if (copy)
            NcFreeAdapterList(copy);
        if (job)
            LocalFree(job);
        return;
    }
    RtlCopyMemory(copy, g_List, size);
    job->List = copy;
    job->Gen = ++g_ProbeGen;
    job->Sig = NcAdapterSignature(copy);
    thread = CreateThread(NULL, 0, ProbeThread, job, 0, NULL);
    if (!thread)
    {
        NcFreeAdapterList(copy);
        LocalFree(job);
        return;
    }
    CloseHandle(thread);
    g_ProbeRunning = TRUE;
    g_ReprobeNeeded = FALSE;
}

/* The shown Internet verdict is only valid for the network it was measured on */
static VOID InvalidateAccess(VOID)
{
    g_ProbeDone = FALSE;
    RtlZeroMemory(&g_Access, sizeof(g_Access));
}

static VOID Refresh(BOOL Probe)
{
    NC_ADAPTER_LIST *list = NcEnumerateAdapters(&g_ListError);

    NcFreeAdapterList(g_List);
    g_List = list;
    NcGetIdentity(&g_Identity);
    NcQueryFeatures(&g_Features);
    /* A different set of adapters, addresses or gateways than the one measured: the old verdict is stale */
    if (g_ProbeDone && NcAdapterSignature(g_List) != g_ProbeSig)
    {
        InvalidateAccess();
        Probe = TRUE;
    }
    if (Probe)
        StartProbe();
    InvalidateRect(g_Main, NULL, TRUE);
}

/* ---- drawing helpers ---- */

static int TextHeight(HDC hdc, HFONT font)
{
    TEXTMETRICW tm;
    HGDIOBJ old = SelectObject(hdc, font);

    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    return tm.tmHeight + tm.tmExternalLeading;
}

/* Draws text at (x, y) and returns its width */
static int Text(HDC hdc, HFONT font, COLORREF color, int x, int y, PCWSTR text)
{
    SIZE size;
    HGDIOBJ old = SelectObject(hdc, font);

    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text, lstrlenW(text));
    GetTextExtentPoint32W(hdc, text, lstrlenW(text), &size);
    SelectObject(hdc, old);
    return size.cx;
}

static int AddLink(HDC hdc, int x, int y, PCWSTR text, int action, int arg, BOOL enabled)
{
    SIZE size;
    int index = g_LinkCount;
    BOOL hot = (g_Hot == index && enabled);
    HGDIOBJ old = SelectObject(hdc, hot ? g_FontLink : g_FontNormal);

    GetTextExtentPoint32W(hdc, text, lstrlenW(text), &size);
    SelectObject(hdc, old);
    Text(hdc, hot ? g_FontLink : g_FontNormal, enabled ? COLOR_LINK : COLOR_GRAY, x, y, text);
    if (g_LinkCount < MAX_LINKS)
    {
        g_Links[index].Rect.left = x;
        g_Links[index].Rect.top = y;
        g_Links[index].Rect.right = x + size.cx;
        g_Links[index].Rect.bottom = y + size.cy;
        g_Links[index].Action = action;
        g_Links[index].Arg = arg;
        g_Links[index].Enabled = enabled;
        g_LinkCount++;
    }
    return size.cx;
}

static COLORREF LevelColor(NC_LEVEL level)
{
    switch (level)
    {
        case NC_LEVEL_INTERNET: return COLOR_GOOD;
        case NC_LEVEL_LOCAL: return COLOR_WARN;
        case NC_LEVEL_NONE: return COLOR_BAD;
        default: return COLOR_UNKNOWN;
    }
}

static VOID DrawLine(HDC hdc, int x1, int x2, int y, COLORREF color, BOOL dashed)
{
    HPEN pen = CreatePen(dashed ? PS_DASH : PS_SOLID, dashed ? 1 : 4, color);
    HGDIOBJ old = SelectObject(hdc, pen);

    MoveToEx(hdc, x1, y, NULL);
    LineTo(hdc, x2, y);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static VOID FillRectColor(HDC hdc, int l, int t, int r, int b, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    RECT rc = { l, t, r, b };

    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
}

/* Original line art, drawn with GDI: a computer, a network hub and a globe */
static VOID DrawComputer(HDC hdc, int x, int y)
{
    FillRectColor(hdc, x, y, x + 56, y + 38, RGB(70, 80, 100));
    FillRectColor(hdc, x + 4, y + 4, x + 52, y + 34, RGB(150, 200, 240));
    FillRectColor(hdc, x + 22, y + 38, x + 34, y + 46, RGB(110, 120, 140));
    FillRectColor(hdc, x + 12, y + 46, x + 44, y + 50, RGB(70, 80, 100));
}

static VOID DrawNetwork(HDC hdc, int x, int y)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(70, 80, 100));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HBRUSH brush = CreateSolidBrush(RGB(150, 200, 240));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);

    MoveToEx(hdc, x + 28, y + 25, NULL); LineTo(hdc, x + 6, y + 6);
    MoveToEx(hdc, x + 28, y + 25, NULL); LineTo(hdc, x + 50, y + 6);
    MoveToEx(hdc, x + 28, y + 25, NULL); LineTo(hdc, x + 6, y + 44);
    MoveToEx(hdc, x + 28, y + 25, NULL); LineTo(hdc, x + 50, y + 44);
    Ellipse(hdc, x + 16, y + 13, x + 40, y + 37);
    Ellipse(hdc, x, y, x + 12, y + 12);
    Ellipse(hdc, x + 44, y, x + 56, y + 12);
    Ellipse(hdc, x, y + 38, x + 12, y + 50);
    Ellipse(hdc, x + 44, y + 38, x + 56, y + 50);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static VOID DrawGlobe(HDC hdc, int x, int y)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(40, 90, 150));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HBRUSH brush = CreateSolidBrush(RGB(120, 190, 235));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);

    Ellipse(hdc, x + 3, y, x + 53, y + 50);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, x + 18, y, x + 38, y + 50);
    MoveToEx(hdc, x + 3, y + 25, NULL); LineTo(hdc, x + 53, y + 25);
    MoveToEx(hdc, x + 28, y, NULL); LineTo(hdc, x + 28, y + 50);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

/* ---- content ---- */

static NC_LEVEL AdapterLevel(const NC_ADAPTER *a, BOOL v6)
{
    /* Link level from the adapter itself; the Internet verdict comes from the probe, per address family. */
    BOOL address = v6 ? a->HasGlobalV6 : a->HasGlobalV4;
    BOOL gateway = v6 ? a->HasGatewayV6 : a->HasGatewayV4;
    NC_LEVEL probed = v6 ? g_Access.V6 : g_Access.V4;

    if (!a->IsUp || !address)
        return NC_LEVEL_NONE;
    if (!gateway)
        return NC_LEVEL_LOCAL;
    if (!g_ProbeDone)
        return NC_LEVEL_UNKNOWN;
    return probed == NC_LEVEL_INTERNET ? NC_LEVEL_INTERNET : NC_LEVEL_LOCAL;
}

static PCWSTR StateText(NC_STATE state)
{
    switch (state)
    {
        case NC_ON: return L"Service running";
        case NC_OFF: return L"Service stopped";
        case NC_UNSUPPORTED: return L"Not available on this system";
        default: return L"Cannot be determined";
    }
}

static COLORREF StateColor(NC_STATE state)
{
    switch (state)
    {
        case NC_ON: return COLOR_GOOD;
        case NC_OFF: return COLOR_WARN;
        default: return COLOR_GRAY;
    }
}

static BOOL AnyConnected(VOID)
{
    ULONG i;

    for (i = 0; g_List && i < g_List->Count; i++)
        if (g_List->Adapters[i].IsUp && !g_List->Adapters[i].IsLoopback && (g_List->Adapters[i].HasGlobalV4 || g_List->Adapters[i].HasGlobalV6))
            return TRUE;
    return FALSE;
}

static VOID PaintLeft(HDC hdc, int height)
{
    int lh = TextHeight(hdc, g_FontNormal) + 8, y = 20;
    BOOL conn = NcToolAvailable(NC_TOOL_CONNECTIONS), inet = NcToolAvailable(NC_TOOL_INTERNET_OPTIONS), fw = NcToolAvailable(NC_TOOL_FIREWALL);

    FillRectColor(hdc, 0, 0, LEFT_WIDTH, height, COLOR_PANE);
    SetBkMode(hdc, TRANSPARENT);
    AddLink(hdc, 16, y, L"Control Panel Home", LA_HOME, 0, TRUE); y += lh + 10;
    AddLink(hdc, 16, y, L"Manage network connections", LA_CONNECTIONS, 0, conn); y += lh;
    AddLink(hdc, 16, y, L"Change adapter settings", LA_CONNECTIONS, 0, conn); y += lh;
    AddLink(hdc, 16, y, L"Diagnose and repair", LA_DIAGNOSE, 0, TRUE); y += lh;
    AddLink(hdc, 16, y, L"Refresh now", LA_REFRESH, 0, TRUE); y += lh + 10;
    Text(hdc, g_FontBold, COLOR_TITLE, 16, y, L"See also"); y += lh;
    AddLink(hdc, 16, y, L"Internet Options", LA_INTERNET_OPTIONS, 0, inet); if (!inet) Text(hdc, g_FontSmall, COLOR_GRAY, 20, y + lh - 6, L"not installed"); y += lh + (inet ? 0 : 8);
    AddLink(hdc, 16, y, L"Windows Firewall", LA_FIREWALL, 0, fw); if (!fw) Text(hdc, g_FontSmall, COLOR_GRAY, 20, y + lh - 6, L"not installed"); y += lh + (fw ? 0 : 8);
    Text(hdc, g_FontNormal, COLOR_GRAY, 16, y, L"Sharing options"); y += lh - 4;
    Text(hdc, g_FontSmall, COLOR_GRAY, 20, y, L"not available yet");
}

static int PaintMap(HDC hdc, int x, int y, int width)
{
    int col = (width - 3 * 56) / 2, x1 = x, x2 = x + col + 56, x3 = x + 2 * (col + 56) - 0;
    int cy = y + 25, gap = 8;
    NC_LEVEL link1 = AnyConnected() ? NC_LEVEL_INTERNET : NC_LEVEL_NONE;
    NC_LEVEL link2 = g_ProbeDone ? ((g_Access.V4 == NC_LEVEL_INTERNET || g_Access.V6 == NC_LEVEL_INTERNET) ? NC_LEVEL_INTERNET :
                                    (AnyConnected() ? NC_LEVEL_LOCAL : NC_LEVEL_NONE)) : NC_LEVEL_UNKNOWN;
    WCHAR network[NC_TEXT * 2] = L"No network";
    ULONG i;

    if (x3 + 56 > x + width)
        x3 = x + width - 56;
    DrawLine(hdc, x1 + 56 + gap, x2 - gap, cy, LevelColor(link1), FALSE);
    DrawLine(hdc, x2 + 56 + gap, x3 - gap, cy, LevelColor(link2), link2 == NC_LEVEL_UNKNOWN);
    DrawComputer(hdc, x1, y);
    DrawNetwork(hdc, x2, y);
    DrawGlobe(hdc, x3, y);

    for (i = 0; g_List && i < g_List->Count; i++)
    {
        const NC_ADAPTER *a = &g_List->Adapters[i];

        if (a->IsUp && !a->IsLoopback && (a->HasGlobalV4 || a->HasGlobalV6))
        {
            StringCchCopyW(network, ARRAYSIZE(network), a->Name);
            break;
        }
    }
    Text(hdc, g_FontBold, 0, x1, y + 58, g_Identity.ComputerName[0] ? g_Identity.ComputerName : L"This computer");
    Text(hdc, g_FontSmall, COLOR_GRAY, x1, y + 76, L"This computer");
    Text(hdc, g_FontBold, 0, x2, y + 58, network);
    Text(hdc, g_FontSmall, COLOR_GRAY, x2, y + 76, g_Identity.Domain[0] ? g_Identity.Domain : L"Network");
    Text(hdc, g_FontBold, 0, x3, y + 58, L"Internet");
    Text(hdc, g_FontSmall, link2 == NC_LEVEL_UNKNOWN ? COLOR_GRAY : LevelColor(link2), x3, y + 76,
         g_ProbeRunning && !g_ProbeDone ? L"checking..." : (link2 == NC_LEVEL_INTERNET ? L"reachable" : (link2 == NC_LEVEL_UNKNOWN ? L"not checked" : L"not reachable")));
    return y + 100;
}

static int PaintContent(HDC hdc, int x0, int width)
{
    int lh = TextHeight(hdc, g_FontNormal) + 4, y = MARGIN - g_Scroll, x = x0 + MARGIN;
    int inner = width - 2 * MARGIN;
    ULONG i;
    WCHAR line[256];
    BOOL any = FALSE;
    NC_LEVEL v4, v6;

    Text(hdc, g_FontTitle, COLOR_TITLE, x, y, L"View your basic network information and set up connections");
    y += TextHeight(hdc, g_FontTitle) + 18;
    y = PaintMap(hdc, x, y, inner);

    Text(hdc, g_FontBold, COLOR_TITLE, x, y, L"View your active networks"); y += lh + 6;
    if (!g_List)
    {
        StringCchPrintfW(line, ARRAYSIZE(line), L"The network adapters cannot be read (error %lu).", g_ListError);
        Text(hdc, g_FontNormal, COLOR_BAD, x + 12, y, line); y += lh;
    }
    for (i = 0; g_List && i < g_List->Count; i++)
    {
        const NC_ADAPTER *a = &g_List->Adapters[i];

        if (!a->IsUp || a->IsLoopback)
            continue;
        any = TRUE;
        v4 = AdapterLevel(a, FALSE);
        v6 = AdapterLevel(a, TRUE);
        Text(hdc, g_FontBold, 0, x + 12, y, a->Name); y += lh;
        StringCchPrintfW(line, ARRAYSIZE(line), L"%s", a->Description);
        Text(hdc, g_FontSmall, COLOR_GRAY, x + 12, y, line); y += lh - 2;
        Text(hdc, g_FontNormal, 0, x + 24, y, L"IPv4 connectivity:");
        Text(hdc, g_FontNormal, LevelColor(v4), x + 190, y, NcLevelText(v4)); y += lh;
        Text(hdc, g_FontNormal, 0, x + 24, y, L"IPv6 connectivity:");
        Text(hdc, g_FontNormal, LevelColor(v6), x + 190, y, NcLevelText(v6)); y += lh;
        {
            int lx = x + 24;

            lx += AddLink(hdc, lx, y, L"View status", LA_STATUS, (int)i, TRUE) + 24;
            AddLink(hdc, lx, y, L"Properties", LA_PROPERTIES, (int)i, NcToolAvailable(NC_TOOL_CONNECTIONS));
        }
        y += lh + 4;
        {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(210, 220, 235));
            HGDIOBJ old = SelectObject(hdc, pen);

            MoveToEx(hdc, x, y - 2, NULL);
            LineTo(hdc, x + inner, y - 2);
            SelectObject(hdc, old);
            DeleteObject(pen);
        }
    }
    if (g_List && !any)
    {
        Text(hdc, g_FontNormal, COLOR_BAD, x + 12, y, L"No network adapter is connected."); y += lh;
    }
    y += 8;
    if (g_ProbeDone)
    {
        StringCchPrintfW(line, ARRAYSIZE(line), L"Internet check at %02u:%02u:%02u: %s", g_ProbeTime.wHour, g_ProbeTime.wMinute, g_ProbeTime.wSecond, g_Access.Detail);
        Text(hdc, g_FontSmall, COLOR_GRAY, x + 12, y, line);
    }
    else
        Text(hdc, g_FontSmall, COLOR_GRAY, x + 12, y, g_ProbeRunning ? L"Checking Internet access (a real connection is tried, an address alone is not access)..." : L"Internet access has not been checked.");
    y += lh + 12;

    Text(hdc, g_FontBold, COLOR_TITLE, x, y, L"Sharing and discovery"); y += lh + 6;
    {
        static const struct { PCWSTR Name; int Which; } rows[] = {
            { L"Network discovery", 0 }, { L"File sharing", 1 }, { L"Printer sharing", 2 }
        };
        int r;

        for (r = 0; r < 3; r++)
        {
            NC_STATE s = (rows[r].Which == 0) ? g_Features.NetworkDiscovery : (rows[r].Which == 1 ? g_Features.FileSharing : g_Features.PrinterSharing);

            Text(hdc, g_FontNormal, 0, x + 12, y, rows[r].Name);
            Text(hdc, g_FontNormal, StateColor(s), x + 190, y, StateText(s));
            y += lh;
        }
    }
    y += 6;
    Text(hdc, g_FontSmall, COLOR_GRAY, x + 12, y, L"The state shows whether the services behind each setting are running; settings cannot be changed here yet.");
    y += lh + MARGIN;
    return y + g_Scroll;
}

static VOID Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps), mem;
    RECT rc;
    HBITMAP bmp, oldBmp;
    int w, h;

    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    mem = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, w, h);
    oldBmp = SelectObject(mem, bmp);
    FillRectColor(mem, 0, 0, w, h, RGB(255, 255, 255));
    SetBkMode(mem, TRANSPARENT);
    g_LinkCount = 0;
    PaintLeft(mem, h);
    g_ContentHeight = PaintContent(mem, LEFT_WIDTH, w - LEFT_WIDTH);
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static VOID UpdateScroll(HWND hwnd)
{
    RECT rc;
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };

    GetClientRect(hwnd, &rc);
    si.nMin = 0;
    si.nMax = g_ContentHeight > 0 ? g_ContentHeight : 0;
    si.nPage = rc.bottom;
    si.nPos = g_Scroll;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

/* ---- text windows (details and diagnostics) ---- */

static LRESULT CALLBACK TextWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                        8, 8, 100, 100, hwnd, (HMENU)100, g_Instance, NULL);
            HWND ok = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 80, 26, hwnd, (HMENU)IDOK, g_Instance, NULL);
            SendMessageW(edit, WM_SETFONT, (WPARAM)g_FontNormal, TRUE);
            SendMessageW(ok, WM_SETFONT, (WPARAM)g_FontNormal, TRUE);
            return 0;
        }
        case WM_SIZE:
            MoveWindow(GetDlgItem(hwnd, 100), 8, 8, LOWORD(lParam) - 16, HIWORD(lParam) - 52, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDOK), LOWORD(lParam) - 92, HIWORD(lParam) - 36, 80, 26, TRUE);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
                DestroyWindow(hwnd);
            return 0;
        case WM_NCSC_TEXT:
            SetWindowTextW(GetDlgItem(hwnd, 100), (PCWSTR)lParam);
            LocalFree((HLOCAL)lParam);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND OpenTextWindow(HWND owner, PCWSTR title, PCWSTR text)
{
    HWND hwnd = CreateWindowExW(0, L"NcscText", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 560, 440, owner, NULL, g_Instance, NULL);

    if (hwnd && text)
        SetWindowTextW(GetDlgItem(hwnd, 100), text);
    return hwnd;
}

static VOID FormatAdapterDetails(const NC_ADAPTER *a, WCHAR *out, UINT count)
{
    WCHAR mac[40], speed[32], speed2[32], line[320];
    ULONG i;

    NcFormatMac(a, mac, ARRAYSIZE(mac));
    NcFormatSpeed(a->TransmitLinkSpeed, speed, ARRAYSIZE(speed));
    NcFormatSpeed(a->ReceiveLinkSpeed, speed2, ARRAYSIZE(speed2));
    out[0] = UNICODE_NULL;
#define ADD(...) do { StringCchPrintfW(line, ARRAYSIZE(line), __VA_ARGS__); StringCchCatW(out, count, line); } while (0)
    ADD(L"Connection:\t\t%s\r\nDescription:\t\t%s\r\nPhysical address:\t%s\r\nStatus:\t\t\t%s\r\n", a->Name, a->Description, mac, a->IsUp ? L"Connected" : L"Disconnected");
    ADD(L"Speed (send/receive):\t%s / %s\r\nMTU:\t\t\t%lu\r\n", speed, speed2, a->Mtu);
    ADD(L"DHCP enabled (IPv4):\t%s\r\n", a->Dhcp4 == NC_ON ? L"Yes" : (a->Dhcp4 == NC_OFF ? L"No" : L"Unknown"));
    if (a->DhcpServer[0])
        ADD(L"DHCP server:\t\t%s\r\n", a->DhcpServer);
    for (i = 0; i < a->AddressCount; i++)
        ADD(L"%s address:\t\t%s/%lu%s\r\n", a->Addresses[i].IsV6 ? L"IPv6" : L"IPv4", a->Addresses[i].Text, a->Addresses[i].PrefixLength,
            a->Addresses[i].IsLinkLocal ? L" (link-local)" : L"");
    if (!a->AddressCount)
        ADD(L"IP addresses:\t\tnone\r\n");
    for (i = 0; i < a->GatewayCount; i++)
        ADD(L"Default gateway:\t%s\r\n", a->Gateways[i]);
    if (!a->GatewayCount)
        ADD(L"Default gateway:\tnone\r\n");
    for (i = 0; i < a->DnsCount; i++)
        ADD(L"DNS server:\t\t%s\r\n", a->DnsServers[i]);
    if (!a->DnsCount)
        ADD(L"DNS servers:\t\tnone\r\n");
    if (a->HasStatistics)
        ADD(L"Bytes received/sent:\t%I64u / %I64u\r\nErrors received/sent:\t%I64u / %I64u\r\n", a->InOctets, a->OutOctets, a->InErrors, a->OutErrors);
    else
        ADD(L"Statistics:\t\tnot available\r\n");
#undef ADD
}

static VOID FormatReport(const NC_DIAG_REPORT *report, WCHAR *out, UINT count)
{
    ULONG i;
    WCHAR line[400];

    StringCchCopyW(out, count, L"Diagnostics of the local network setup. Nothing is changed.\r\n\r\n");
    for (i = 0; i < report->Done && i < NC_DIAG_STEP_COUNT; i++)
    {
        StringCchPrintfW(line, ARRAYSIZE(line), L"%lu. %s: %s\r\n     %s\r\n", i + 1, report->Steps[i].Title, NcDiagResultText(report->Steps[i].Result), report->Steps[i].Detail);
        StringCchCatW(out, count, line);
    }
    if (report->Done < NC_DIAG_STEP_COUNT)
        StringCchCatW(out, count, L"\r\nRunning...\r\n");
    else
        StringCchCatW(out, count, L"\r\nDone. \"Inconclusive\" means the remote end may simply not answer ping; \"Internet access\" is decided only by a real connection.\r\n");
}

typedef struct _DIAG_JOB
{
    HWND Window;
} DIAG_JOB;

static VOID CALLBACK DiagProgress(const NC_DIAG_REPORT *Report, PVOID Context)
{
    DIAG_JOB *job = Context;
    UINT count = 4096;
    WCHAR *text = LocalAlloc(LPTR, count * sizeof(WCHAR));

    if (!text)
        return;
    FormatReport(Report, text, count);
    if (!PostMessageW(job->Window, WM_NCSC_TEXT, 0, (LPARAM)text))
        LocalFree(text);
}

static DWORD WINAPI DiagThread(LPVOID Param)
{
    DIAG_JOB *job = Param;
    NC_DIAG_REPORT *report = LocalAlloc(LPTR, sizeof(*report));

    if (report)
    {
        NcRunDiagnostics(report, DiagProgress, job);
        LocalFree(report);
    }
    LocalFree(job);
    return 0;
}

static VOID RunDiagnosticsWindow(HWND owner)
{
    DIAG_JOB *job = LocalAlloc(LPTR, sizeof(*job));
    HANDLE thread;

    job->Window = OpenTextWindow(owner, L"Network diagnostics", L"Running diagnostics...");
    if (!job->Window)
    {
        LocalFree(job);
        return;
    }
    thread = CreateThread(NULL, 0, DiagThread, job, 0, NULL);
    if (!thread)
    {
        LocalFree(job);
        SetWindowTextW(GetDlgItem(job->Window, 100), L"The diagnostics thread could not be started.");
        return;
    }
    CloseHandle(thread);
}

/* ---- actions ---- */

static VOID Failed(HWND hwnd, PCWSTR what)
{
    MessageBoxW(hwnd, what, L"Network and Sharing Center", MB_OK | MB_ICONWARNING);
}

static VOID DoLink(HWND hwnd, const LINK *link)
{
    WCHAR text[1600];

    if (!link->Enabled)
        return;
    switch (link->Action)
    {
        case LA_HOME:
            if ((INT_PTR)ShellExecuteW(hwnd, L"open", L"control.exe", NULL, NULL, SW_SHOWNORMAL) <= 32)
                Failed(hwnd, L"The Control Panel could not be started.");
            break;
        case LA_CONNECTIONS:
        case LA_PROPERTIES:
            if (!NcLaunchTool(hwnd, NC_TOOL_CONNECTIONS))
                Failed(hwnd, L"Network Connections could not be opened.");
            break;
        case LA_INTERNET_OPTIONS:
            if (!NcLaunchTool(hwnd, NC_TOOL_INTERNET_OPTIONS))
                Failed(hwnd, L"Internet Options could not be opened.");
            break;
        case LA_FIREWALL:
            if (!NcLaunchTool(hwnd, NC_TOOL_FIREWALL))
                Failed(hwnd, L"The firewall settings could not be opened.");
            break;
        case LA_DIAGNOSE:
            RunDiagnosticsWindow(hwnd);
            break;
        case LA_REFRESH:
            g_ProbeDone = FALSE;
            Refresh(TRUE);
            break;
        case LA_STATUS:
            if (g_List && (ULONG)link->Arg < g_List->Count)
            {
                FormatAdapterDetails(&g_List->Adapters[link->Arg], text, ARRAYSIZE(text));
                OpenTextWindow(hwnd, L"Connection status", text);
            }
            break;
    }
}

static int HitTest(int x, int y)
{
    int i;

    for (i = 0; i < g_LinkCount; i++)
        if (g_Links[i].Enabled && x >= g_Links[i].Rect.left && x < g_Links[i].Rect.right && y >= g_Links[i].Rect.top && y < g_Links[i].Rect.bottom)
            return i;
    return -1;
}

static VOID MakeFonts(VOID)
{
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    LOGFONTW lf;

    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    lf = ncm.lfMessageFont;
    g_FontNormal = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    g_FontBold = CreateFontIndirectW(&lf);
    lf = ncm.lfMessageFont;
    lf.lfUnderline = TRUE;
    g_FontLink = CreateFontIndirectW(&lf);
    lf = ncm.lfMessageFont;
    lf.lfHeight = lf.lfHeight * 4 / 3;
    g_FontTitle = CreateFontIndirectW(&lf);
    lf = ncm.lfMessageFont;
    lf.lfHeight = lf.lfHeight * 5 / 6;
    g_FontSmall = CreateFontIndirectW(&lf);
}

/* Called by the stack on its own thread: only post, never touch the window state here */
static VOID NETIOAPI_API_ InterfaceChanged(PVOID Context, PMIB_IPINTERFACE_ROW Row, MIB_NOTIFICATION_TYPE Type)
{
    PostMessageW(g_Main, WM_NCSC_NETCHANGE, 0, 0);
}

static VOID NETIOAPI_API_ AddressChanged(PVOID Context, PMIB_UNICASTIPADDRESS_ROW Row, MIB_NOTIFICATION_TYPE Type)
{
    PostMessageW(g_Main, WM_NCSC_NETCHANGE, 0, 0);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
            g_Main = hwnd;
            MakeFonts();
            Refresh(TRUE);
            SetTimer(hwnd, TIMER_REFRESH, REFRESH_MS, NULL);
            /* react at once to an interface or address change; where the stack does not support the
             * notification the 15 second refresh still notices the change through the signature */
            if (NotifyIpInterfaceChange(AF_UNSPEC, InterfaceChanged, NULL, FALSE, &g_NotifyInterface) != NO_ERROR)
                g_NotifyInterface = NULL;
            if (NotifyUnicastIpAddressChange(AF_UNSPEC, AddressChanged, NULL, FALSE, &g_NotifyAddress) != NO_ERROR)
                g_NotifyAddress = NULL;
            return 0;
        case WM_TIMER:
            if (wParam == TIMER_CHANGE)
            {
                KillTimer(hwnd, TIMER_CHANGE);
                Refresh(TRUE);
                return 0;
            }
            g_Ticks++;
            Refresh((g_Ticks % PROBE_EVERY) == 0);
            return 0;
        case WM_NCSC_PROBED:
        {
            PROBE_RESULT *result = (PROBE_RESULT *)lParam;
            BOOL current = (result->Gen == g_ProbeGen) && !g_ReprobeNeeded &&
                           (g_List && result->Sig == NcAdapterSignature(g_List));

            g_ProbeRunning = FALSE;
            if (current)
            {
                g_Access = result->Access;
                g_ProbeSig = result->Sig;
                g_ProbeDone = TRUE;
                GetLocalTime(&g_ProbeTime);
            }
            else
            {
                /* measured on a network that has changed since: do not show it, check again */
                InvalidateAccess();
                StartProbe();
            }
            LocalFree(result);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        case WM_NCSC_NETCHANGE:
            /* several notifications arrive together: refresh once, and stop showing the old verdict now */
            InvalidateAccess();
            InvalidateRect(hwnd, NULL, TRUE);
            SetTimer(hwnd, TIMER_CHANGE, CHANGE_DELAY_MS, NULL);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint(hwnd);
            UpdateScroll(hwnd);
            return 0;
        case WM_SIZE:
            UpdateScroll(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        case WM_VSCROLL:
        {
            SCROLLINFO si = { sizeof(si), SIF_ALL };
            int pos;

            GetScrollInfo(hwnd, SB_VERT, &si);
            pos = si.nPos;
            switch (LOWORD(wParam))
            {
                case SB_LINEUP: pos -= 24; break;
                case SB_LINEDOWN: pos += 24; break;
                case SB_PAGEUP: pos -= si.nPage; break;
                case SB_PAGEDOWN: pos += si.nPage; break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
            }
            pos = max(0, min(pos, (int)(si.nMax - si.nPage + 1)));
            if (pos != g_Scroll)
            {
                g_Scroll = pos;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            SendMessageW(hwnd, WM_VSCROLL, (short)HIWORD(wParam) > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
            return 0;
        case WM_MOUSEMOVE:
        {
            int hot = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));

            if (hot != g_Hot)
            {
                g_Hot = hot;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            SetCursor(LoadCursorW(NULL, hot >= 0 ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_LBUTTONUP:
        {
            int hit = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));

            if (hit >= 0)
            {
                LINK link = g_Links[hit];

                DoLink(hwnd, &link);
            }
            return 0;
        }
        case WM_DESTROY:
            if (g_NotifyInterface)
                CancelMibChangeNotify2(g_NotifyInterface);
            if (g_NotifyAddress)
                CancelMibChangeNotify2(g_NotifyAddress);
            g_NotifyInterface = g_NotifyAddress = NULL;
            KillTimer(hwnd, TIMER_REFRESH);
            KillTimer(hwnd, TIMER_CHANGE);
            NcFreeAdapterList(g_List);
            g_List = NULL;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nShow)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    MSG msg;
    HWND hwnd;

    g_Instance = hInstance;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAINICON));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"LeewayNetworkCenter";
    if (!RegisterClassExW(&wc))
        return 1;
    wc.lpfnWndProc = TextWndProc;
    wc.hIcon = wc.hIconSm = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NcscText";
    if (!RegisterClassExW(&wc))
        return 1;

    hwnd = CreateWindowExW(0, L"LeewayNetworkCenter", L"Network and Sharing Center", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 900, 620, NULL, NULL, hInstance, NULL);
    if (!hwnd)
        return 1;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
