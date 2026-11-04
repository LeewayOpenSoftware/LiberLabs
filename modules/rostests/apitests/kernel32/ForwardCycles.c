/*
 * PROJECT:     LiberNT kernel32 API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Follow every export forwarder of kernel32 and kernelbase and fail on a cycle
 */

#include "precomp.h"

#define MAX_FORWARD_DEPTH 8

typedef struct _EXPORT_VIEW
{
    PBYTE Base;
    PIMAGE_EXPORT_DIRECTORY Directory;
    DWORD DirectoryRva;
    DWORD DirectorySize;
} EXPORT_VIEW;

static
BOOL
GetExportView(HMODULE Module, EXPORT_VIEW *View)
{
    PIMAGE_DOS_HEADER Dos = (PIMAGE_DOS_HEADER)Module;
    PIMAGE_NT_HEADERS Nt;
    PIMAGE_DATA_DIRECTORY Data;

    if (!Module || Dos->e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;
    Nt = (PIMAGE_NT_HEADERS)((PBYTE)Module + Dos->e_lfanew);
    if (Nt->Signature != IMAGE_NT_SIGNATURE)
        return FALSE;
    Data = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!Data->VirtualAddress)
        return FALSE;

    View->Base = (PBYTE)Module;
    View->Directory = (PIMAGE_EXPORT_DIRECTORY)(View->Base + Data->VirtualAddress);
    View->DirectoryRva = Data->VirtualAddress;
    View->DirectorySize = Data->Size;
    return TRUE;
}

/* The forwarder text of an export, or NULL when the export has code */
static
PCSTR
GetForwarder(const EXPORT_VIEW *View, DWORD Index)
{
    PDWORD Functions = (PDWORD)(View->Base + View->Directory->AddressOfFunctions);
    DWORD Rva = Functions[Index];

    if (Rva >= View->DirectoryRva && Rva < View->DirectoryRva + View->DirectorySize)
        return (PCSTR)(View->Base + Rva);
    return NULL;
}

static
BOOL
FindExport(const EXPORT_VIEW *View, PCSTR Name, DWORD *Index)
{
    PDWORD Names = (PDWORD)(View->Base + View->Directory->AddressOfNames);
    PWORD Ordinals = (PWORD)(View->Base + View->Directory->AddressOfNameOrdinals);
    DWORD i;

    for (i = 0; i < View->Directory->NumberOfNames; i++)
    {
        if (!strcmp((PCSTR)(View->Base + Names[i]), Name))
        {
            *Index = Ordinals[i];
            return TRUE;
        }
    }
    return FALSE;
}

typedef enum _CHAIN_RESULT
{
    ChainEnds,
    ChainCycle,
    ChainDangling,
    ChainUnresolved
} CHAIN_RESULT;

/* Follows "dll.name" until real code is reached */
static
CHAIN_RESULT
FollowChain(PCSTR Forwarder, PCSTR Start)
{
    char Dll[64], Name[256];
    PCSTR Dot;
    HMODULE Module;
    EXPORT_VIEW View;
    DWORD Index;
    int Depth;

    for (Depth = 0; Depth < MAX_FORWARD_DEPTH; Depth++)
    {
        Dot = strchr(Forwarder, '.');
        if (!Dot || (SIZE_T)(Dot - Forwarder) >= sizeof(Dll) || strlen(Dot + 1) >= sizeof(Name))
            return ChainUnresolved;

        memcpy(Dll, Forwarder, Dot - Forwarder);
        Dll[Dot - Forwarder] = ANSI_NULL;
        strcpy(Name, Dot + 1);

        /* Ordinal forwarders are left alone */
        if (Name[0] == '#')
            return ChainUnresolved;

        Module = GetModuleHandleA(Dll);
        if (!Module || !GetExportView(Module, &View))
            return ChainUnresolved;

        if (!FindExport(&View, Name, &Index))
            return ChainDangling;

        Forwarder = GetForwarder(&View, Index);
        if (!Forwarder)
            return ChainEnds;

        if (!_stricmp(Forwarder, Start))
            return ChainCycle;
    }

    return ChainCycle;
}

static
void
CheckModule(PCSTR ModuleName, DWORD *Total, DWORD *Dangling)
{
    HMODULE Module = GetModuleHandleA(ModuleName);
    EXPORT_VIEW View;
    PDWORD Names;
    PWORD Ordinals;
    DWORD i;

    ok(Module != NULL, "%s is not loaded\n", ModuleName);
    if (!Module || !GetExportView(Module, &View))
        return;

    Names = (PDWORD)(View.Base + View.Directory->AddressOfNames);
    Ordinals = (PWORD)(View.Base + View.Directory->AddressOfNameOrdinals);

    for (i = 0; i < View.Directory->NumberOfNames; i++)
    {
        PCSTR Name = (PCSTR)(View.Base + Names[i]);
        PCSTR Forwarder = GetForwarder(&View, Ordinals[i]);
        CHAIN_RESULT Result;

        if (!Forwarder)
            continue;

        (*Total)++;
        Result = FollowChain(Forwarder, Forwarder);
        ok(Result != ChainCycle, "%s!%s forwards to %s, which never reaches code\n", ModuleName, Name, Forwarder);
        if (Result == ChainDangling)
        {
            (*Dangling)++;
            trace("%s!%s forwards to %s, which does not exist\n", ModuleName, Name, Forwarder);
        }
    }
}

/* Exports whose code lives in kernelbase: kernel32 must keep them, as forwarders */
static PCSTR KernelBaseOwned[] =
{
    "CompareFileTime",
    "FileTimeToSystemTime",
    "GetSystemTime",
    "GetSystemTimeAsFileTime",
    "GetSystemTimePreciseAsFileTime",
    "SystemTimeToFileTime",
};

static
void
CheckKernelBaseOwned(void)
{
    HMODULE Kernel32 = GetModuleHandleA("kernel32.dll");
    HMODULE KernelBase = GetModuleHandleA("kernelbase.dll");
    EXPORT_VIEW View32, ViewBase;
    char Expected[300];
    DWORD i, Index;

    if (!Kernel32 || !KernelBase ||
        !GetExportView(Kernel32, &View32) || !GetExportView(KernelBase, &ViewBase))
    {
        skip("kernel32 or kernelbase export directory unavailable\n");
        return;
    }

    for (i = 0; i < ARRAYSIZE(KernelBaseOwned); i++)
    {
        PCSTR Name = KernelBaseOwned[i];
        PCSTR Forwarder;
        FARPROC From32, FromBase;

        if (!FindExport(&View32, Name, &Index))
        {
            ok(0, "kernel32 does not export %s\n", Name);
            continue;
        }
        Forwarder = GetForwarder(&View32, Index);
        StringCbPrintfA(Expected, sizeof(Expected), "kernelbase.%s", Name);
        ok(Forwarder && !_stricmp(Forwarder, Expected),
           "kernel32!%s forwards to %s, expected %s\n", Name, Forwarder ? Forwarder : "(code)", Expected);

        if (!FindExport(&ViewBase, Name, &Index))
        {
            ok(0, "kernelbase does not export %s\n", Name);
            continue;
        }
        Forwarder = GetForwarder(&ViewBase, Index);
        ok(Forwarder == NULL, "kernelbase!%s forwards to %s, expected code\n", Name, Forwarder);

        From32 = GetProcAddress(Kernel32, Name);
        FromBase = GetProcAddress(KernelBase, Name);
        ok(From32 != NULL, "GetProcAddress(kernel32, %s) failed\n", Name);
        ok(From32 == FromBase, "%s: kernel32 resolves to %p, kernelbase to %p\n", Name, From32, FromBase);
    }
}

/* The forwarded conversions behave as they did when kernel32 owned them */
static
void
CheckTimeConversions(void)
{
    SYSTEMTIME In = { 2026, 11, 0, 4, 23, 22, 46, 123 }, Out, Now;
    FILETIME Ft, Later, Bad;
    ULARGE_INTEGER Value;

    ok(SystemTimeToFileTime(&In, &Ft), "SystemTimeToFileTime failed, error %lu\n", GetLastError());
    ok(FileTimeToSystemTime(&Ft, &Out), "FileTimeToSystemTime failed, error %lu\n", GetLastError());
    ok(Out.wYear == 2026 && Out.wMonth == 11 && Out.wDay == 4 && Out.wHour == 23 &&
       Out.wMinute == 22 && Out.wSecond == 46 && Out.wMilliseconds == 123,
       "round trip gave %u-%u-%u %u:%u:%u.%u\n", Out.wYear, Out.wMonth, Out.wDay,
       Out.wHour, Out.wMinute, Out.wSecond, Out.wMilliseconds);
    ok(Out.wDayOfWeek == 3, "2026-11-04 is a Wednesday, got day %u\n", Out.wDayOfWeek);

    In.wMonth = 13;
    SetLastError(0xdeadbeef);
    ok(!SystemTimeToFileTime(&In, &Bad), "month 13 accepted\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "month 13: error %lu\n", GetLastError());

    Bad.dwLowDateTime = 0;
    Bad.dwHighDateTime = 0x80000000;
    SetLastError(0xdeadbeef);
    ok(!FileTimeToSystemTime(&Bad, &Out), "negative FILETIME accepted\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "negative FILETIME: error %lu\n", GetLastError());

    Value.LowPart = Ft.dwLowDateTime;
    Value.HighPart = Ft.dwHighDateTime;
    Value.QuadPart += 1;
    Later.dwLowDateTime = Value.LowPart;
    Later.dwHighDateTime = Value.HighPart;
    ok(CompareFileTime(&Ft, &Later) == -1, "earlier vs later\n");
    ok(CompareFileTime(&Later, &Ft) == 1, "later vs earlier\n");
    ok(CompareFileTime(&Ft, &Ft) == 0, "equal times\n");

    GetSystemTimeAsFileTime(&Ft);
    GetSystemTime(&Now);
    ok(FileTimeToSystemTime(&Ft, &Out), "FileTimeToSystemTime(now) failed\n");
    ok(Out.wYear == Now.wYear && Out.wMonth == Now.wMonth && Out.wDay == Now.wDay,
       "GetSystemTime %u-%u-%u disagrees with GetSystemTimeAsFileTime %u-%u-%u\n",
       Now.wYear, Now.wMonth, Now.wDay, Out.wYear, Out.wMonth, Out.wDay);
}

START_TEST(ForwardCycles)
{
    DWORD Total = 0, Dangling = 0;

    CheckModule("kernel32.dll", &Total, &Dangling);
    CheckModule("kernelbase.dll", &Total, &Dangling);

    trace("%lu forwarders followed in kernel32 and kernelbase, %lu with a missing target\n", Total, Dangling);

    CheckKernelBaseOwned();
    CheckTimeConversions();
}
