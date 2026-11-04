/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UTC system time queries and FILETIME/SYSTEMTIME conversions.
 *              KernelBase owns them; kernel32 forwards its exports here
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <ddk/wdm.h>

#define TimeSharedUserData ((volatile const KUSER_SHARED_DATA *)0x7ffe0000)

/* Same read as KiReadSystemTime: one atomic read on 64-bit, a matched pair otherwise */
static
LARGE_INTEGER
ReadSharedTime(
    _In_ volatile const KSYSTEM_TIME *Time)
{
    LARGE_INTEGER Value;

#ifdef _WIN64
    Value.QuadPart = *(volatile const LONG64 *)Time;
#else
    for (;;)
    {
        Value.HighPart = Time->High1Time;
        Value.LowPart = Time->LowPart;
        if (Value.HighPart == Time->High2Time)
            break;
        YieldProcessor();
    }
#endif
    return Value;
}

static
VOID
TimeFieldsToSystemTime(
    _In_ const TIME_FIELDS *TimeFields,
    _Out_ LPSYSTEMTIME SystemTime)
{
    SystemTime->wYear = TimeFields->Year;
    SystemTime->wMonth = TimeFields->Month;
    SystemTime->wDay = TimeFields->Day;
    SystemTime->wHour = TimeFields->Hour;
    SystemTime->wMinute = TimeFields->Minute;
    SystemTime->wSecond = TimeFields->Second;
    SystemTime->wMilliseconds = TimeFields->Milliseconds;
    SystemTime->wDayOfWeek = TimeFields->Weekday;
}

LONG
WINAPI
CompareFileTime(
    _In_ CONST FILETIME *FileTime1,
    _In_ CONST FILETIME *FileTime2)
{
    ULARGE_INTEGER Time1, Time2;

    Time1.LowPart = FileTime1->dwLowDateTime;
    Time1.HighPart = FileTime1->dwHighDateTime;
    Time2.LowPart = FileTime2->dwLowDateTime;
    Time2.HighPart = FileTime2->dwHighDateTime;

    if (Time1.QuadPart < Time2.QuadPart)
        return -1;
    if (Time1.QuadPart == Time2.QuadPart)
        return 0;
    return 1;
}

VOID
WINAPI
GetSystemTimeAsFileTime(
    _Out_ LPFILETIME FileTime)
{
    LARGE_INTEGER SystemTime = ReadSharedTime(&TimeSharedUserData->SystemTime);

    FileTime->dwLowDateTime = SystemTime.LowPart;
    FileTime->dwHighDateTime = SystemTime.HighPart;
}

VOID
WINAPI
GetSystemTimePreciseAsFileTime(
    _Out_ LPFILETIME FileTime)
{
    LARGE_INTEGER SystemTime;

    SystemTime.QuadPart = RtlGetSystemTimePrecise();

    FileTime->dwLowDateTime = SystemTime.LowPart;
    FileTime->dwHighDateTime = SystemTime.HighPart;
}

BOOL
WINAPI
SystemTimeToFileTime(
    _In_ CONST SYSTEMTIME *SystemTime,
    _Out_ LPFILETIME FileTime)
{
    TIME_FIELDS TimeFields;
    LARGE_INTEGER Time;

    TimeFields.Year = SystemTime->wYear;
    TimeFields.Month = SystemTime->wMonth;
    TimeFields.Day = SystemTime->wDay;
    TimeFields.Hour = SystemTime->wHour;
    TimeFields.Minute = SystemTime->wMinute;
    TimeFields.Second = SystemTime->wSecond;
    TimeFields.Milliseconds = SystemTime->wMilliseconds;

    if (!RtlTimeFieldsToTime(&TimeFields, &Time))
    {
        SetLastError(RtlNtStatusToDosError(STATUS_INVALID_PARAMETER));
        return FALSE;
    }

    FileTime->dwLowDateTime = Time.LowPart;
    FileTime->dwHighDateTime = Time.HighPart;
    return TRUE;
}

BOOL
WINAPI
FileTimeToSystemTime(
    _In_ CONST FILETIME *FileTime,
    _Out_ LPSYSTEMTIME SystemTime)
{
    TIME_FIELDS TimeFields;
    LARGE_INTEGER Time;

    Time.LowPart = FileTime->dwLowDateTime;
    Time.HighPart = FileTime->dwHighDateTime;
    if (Time.QuadPart < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    RtlTimeToTimeFields(&Time, &TimeFields);
    TimeFieldsToSystemTime(&TimeFields, SystemTime);
    return TRUE;
}

VOID
WINAPI
GetSystemTime(
    _Out_ LPSYSTEMTIME SystemTime)
{
    LARGE_INTEGER Time = ReadSharedTime(&TimeSharedUserData->SystemTime);
    TIME_FIELDS TimeFields;

    RtlTimeToTimeFields(&Time, &TimeFields);
    TimeFieldsToSystemTime(&TimeFields, SystemTime);
}
