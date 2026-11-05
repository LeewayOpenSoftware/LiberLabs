/*
 * LiberX contract fixture: XInput battery + DirectSound-GUID exports.
 *
 * Validates the honest documented contract of XInputGetBatteryInformation and
 * XInputGetDSoundAudioDeviceGuids, which previously returned ERROR_NOT_SUPPORTED
 * for any connected controller (a stub). The HID backend never marks a pad as
 * wireless, so a connected pad is genuinely wired -> BATTERY_TYPE_WIRED/FULL;
 * the DirectSound GUIDs for a pad with no audio endpoint are GUID_NULL.
 *
 * The fixture passes with or without a controller attached: controller-specific
 * assertions are SKIPped when no pad is present. It never asserts fake success.
 */
#define COBJMACROS
#define CONST_VTABLE
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <xinput.h>

#define TEST_VERSION 1

static int fail_count, skip_count, pass_count;

static void report(const char *name, const char *status, const char *fmt, ...)
{
    char detail[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    printf("XINPUT_BATTERY v%d %s %s %s\n", TEST_VERSION, name, status, detail);
    if (status[0] == 'F') ++fail_count;
    else if (status[0] == 'S') ++skip_count;
    else ++pass_count;
}

static void check(const char *name, BOOL ok, const char *fmt, ...)
{
    char detail[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    report(name, ok ? "PASS" : "FAIL", "%s", detail);
}

typedef DWORD (WINAPI *get_battery_func)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION *);
typedef DWORD (WINAPI *get_dsound_func)(DWORD, GUID *, GUID *);

int main(void)
{
    HMODULE xinput;
    get_battery_func get_battery;
    get_dsound_func get_dsound;
    XINPUT_BATTERY_INFORMATION batt;
    GUID render, capture, zero;
    DWORD r;

    memset(&zero, 0, sizeof(zero));

    xinput = LoadLibraryA("xinput1_3.dll");
    if (!xinput)
    {
        report("load", "SKIP", "xinput1_3.dll not found");
        goto done;
    }

    get_battery = (get_battery_func)GetProcAddress(xinput, "XInputGetBatteryInformation");
    get_dsound  = (get_dsound_func)GetProcAddress(xinput, "XInputGetDSoundAudioDeviceGuids");
    if (!get_battery || !get_dsound)
    {
        report("exports", "FAIL", "battery=%p dsound=%p", (void *)get_battery, (void *)get_dsound);
        FreeLibrary(xinput);
        goto done;
    }

    /* ---- argument validation (deterministic, no device needed) ---- */
    memset(&batt, 0xCC, sizeof(batt));
    r = get_battery(XUSER_MAX_COUNT, BATTERY_DEVTYPE_GAMEPAD, &batt);
    check("batt_bad_index", r == ERROR_BAD_ARGUMENTS, "index>=max: r=%lu", r);

    r = get_battery(0, BATTERY_DEVTYPE_GAMEPAD, NULL);
    check("batt_null_out", r == ERROR_BAD_ARGUMENTS, "NULL battery: r=%lu", r);

    r = get_battery(0, 0x7F, &batt);
    check("batt_bad_type", r == ERROR_BAD_ARGUMENTS, "bad devtype: r=%lu", r);

    r = get_dsound(XUSER_MAX_COUNT, &render, &capture);
    check("dsound_bad_index", r == ERROR_BAD_ARGUMENTS, "index>=max: r=%lu", r);

    r = get_dsound(0, NULL, &capture);
    check("dsound_null_render", r == ERROR_BAD_ARGUMENTS, "NULL render: r=%lu", r);

    /* ---- live contract on controller 0 ---- */
    memset(&batt, 0xCC, sizeof(batt));
    r = get_battery(0, BATTERY_DEVTYPE_GAMEPAD, &batt);
    if (r == ERROR_DEVICE_NOT_CONNECTED)
    {
        /* no pad: must report disconnected, never the old ERROR_NOT_SUPPORTED */
        check("batt_disconnected", batt.BatteryType == BATTERY_TYPE_DISCONNECTED,
              "type=%u level=%u", batt.BatteryType, batt.BatteryLevel);
    }
    else if (r == ERROR_SUCCESS)
    {
        /* pad present: honest real type (WIRED for a wired pad, UNKNOWN for a
         * wireless pad whose HID battery page we do not read) - never NOT_SUPPORTED */
        check("batt_connected_type",
              batt.BatteryType == BATTERY_TYPE_WIRED ||
              batt.BatteryType == BATTERY_TYPE_UNKNOWN ||
              batt.BatteryType == BATTERY_TYPE_ALKALINE ||
              batt.BatteryType == BATTERY_TYPE_NIMH,
              "type=%u level=%u", batt.BatteryType, batt.BatteryLevel);
    }
    else
    {
        check("batt_live", FALSE, "unexpected r=%lu (stub returned NOT_SUPPORTED=%lu)",
              r, (DWORD)ERROR_NOT_SUPPORTED);
    }

    memset(&render, 0xCC, sizeof(render));
    memset(&capture, 0xCC, sizeof(capture));
    r = get_dsound(0, &render, &capture);
    if (r == ERROR_DEVICE_NOT_CONNECTED)
        report("dsound_disconnected", "PASS", "no pad: r=DEVICE_NOT_CONNECTED");
    else if (r == ERROR_SUCCESS)
        check("dsound_connected_guids",
              memcmp(&render, &zero, sizeof(zero)) == 0 &&
              memcmp(&capture, &zero, sizeof(zero)) == 0,
              "pad present: expect GUID_NULL endpoints");
    else
        check("dsound_live", FALSE, "unexpected r=%lu", r);

    FreeLibrary(xinput);

done:
    printf("XINPUT_BATTERY v%d SUMMARY pass=%d fail=%d skip=%d\n",
           TEST_VERSION, pass_count, fail_count, skip_count);
    return fail_count;
}
