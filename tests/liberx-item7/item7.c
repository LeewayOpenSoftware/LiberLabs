#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define SUITE_VERSION 1

static const GUID clsid_xaudio2_7 = {0x5a508685, 0xa254, 0x4fba, {0x9b, 0x82, 0x9a, 0x24, 0xb0, 0x03, 0x06, 0xaf}};
static const GUID iid_xaudio2_7 = {0x8bcf1f58, 0x9fe7, 0x4583, {0x8a, 0xc6, 0xe2, 0xad, 0xc4, 0x65, 0xc8, 0xbb}};
static const GUID iid_class_factory = {0x00000001, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static int pass_count, fail_count, skip_count;

static void report(const char *group, const char *name, const char *status, const char *fmt, ...)
{
    char detail[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    printf("ITEM7 v%d %s %s %s %s\n", SUITE_VERSION, group, name, status, detail);
    if (status[0] == 'F') ++fail_count;
    else if (status[0] == 'S') ++skip_count;
    else ++pass_count;
}

static void **vtable_of(void *object)
{
    return *(void ***)object;
}

typedef ULONG (WINAPI *release_func)(void *);
typedef HRESULT (WINAPI *get_class_object_func)(const GUID *, const GUID *, void **);
typedef HRESULT (WINAPI *class_create_func)(void *, void *, const GUID *, void **);
typedef HRESULT (WINAPI *xa2_initialize27_func)(void *, UINT32, UINT32);
typedef HRESULT (WINAPI *xa2_device_count27_func)(void *, UINT32 *);
typedef HRESULT (WINAPI *xa2_mastering27_func)(void *, void **, UINT32, UINT32, UINT32, UINT32, const void *);
typedef HRESULT (WINAPI *xa2_mastering28_func)(void *, void **, UINT32, UINT32, UINT32, const WCHAR *, const void *, int);
typedef HRESULT (WINAPI *xa2_create_func)(void **, UINT32, UINT32);
typedef HRESULT (WINAPI *x3d_init_func)(UINT32, float, void *);

static void test_xaudio27(void)
{
    get_class_object_func get_class_object;
    HMODULE module = LoadLibraryA("xaudio2_7.dll");
    void *factory = NULL, *xa = NULL, *voice = NULL;
    UINT32 devices = 0;
    HRESULT hr;
    char path[MAX_PATH] = "";

    if (!module || !(get_class_object = (get_class_object_func)GetProcAddress(module, "DllGetClassObject")))
    {
        report("xaudio2_7", "load", "FAIL", "xaudio2_7.dll or DllGetClassObject missing, error %lu", GetLastError());
        return;
    }
    GetModuleFileNameA(module, path, sizeof(path));
    report("xaudio2_7", "load", "PASS", "%s", path);
    report("xaudio2_7", "exports-registration", GetProcAddress(module, "DllRegisterServer")
            && GetProcAddress(module, "DllCanUnloadNow") ? "PASS" : "FAIL", "DllRegisterServer and DllCanUnloadNow");

    hr = get_class_object(&clsid_xaudio2_7, &iid_class_factory, &factory);
    report("xaudio2_7", "class-factory", SUCCEEDED(hr) && factory ? "PASS" : "FAIL", "hr=%#lx", hr);
    if (FAILED(hr) || !factory) return;
    hr = ((class_create_func)vtable_of(factory)[3])(factory, NULL, &iid_xaudio2_7, &xa);
    ((release_func)vtable_of(factory)[2])(factory);
    report("xaudio2_7", "create-instance", SUCCEEDED(hr) && xa ? "PASS" : "FAIL", "hr=%#lx", hr);
    if (FAILED(hr) || !xa) return;

    hr = ((xa2_device_count27_func)vtable_of(xa)[3])(xa, &devices);
    report("xaudio2_7", "device-count", SUCCEEDED(hr) ? "PASS" : "FAIL", "hr=%#lx count=%u", hr, devices);
    hr = ((xa2_initialize27_func)vtable_of(xa)[5])(xa, 0, 1);
    report("xaudio2_7", "initialize", SUCCEEDED(hr) ? "PASS" : "FAIL", "hr=%#lx", hr);
    if (SUCCEEDED(hr))
    {
        hr = ((xa2_mastering27_func)vtable_of(xa)[10])(xa, &voice, 2, 48000, 0, 0, NULL);
        report("xaudio2_7", "mastering-voice", SUCCEEDED(hr) ? "PASS" : "SKIP",
                "hr=%#lx (a failure needs an audio endpoint and is not a version result)", hr);
        if (SUCCEEDED(hr) && voice)
        {
            hr = ((HRESULT (WINAPI *)(void *))vtable_of(xa)[11])(xa);
            report("xaudio2_7", "start-engine", SUCCEEDED(hr) ? "PASS" : "FAIL", "hr=%#lx", hr);
            ((void (WINAPI *)(void *))vtable_of(xa)[12])(xa);
            ((void (WINAPI *)(void *))vtable_of(voice)[21])(voice);
        }
    }
    ((release_func)vtable_of(xa)[2])(xa);
}

static void test_xaudio28(void)
{
    HMODULE module = LoadLibraryA("xaudio2_8.dll");
    xa2_create_func create;
    void *xa = NULL, *voice = NULL;
    HRESULT hr;
    char path[MAX_PATH] = "";

    if (!module || !(create = (xa2_create_func)GetProcAddress(module, "XAudio2Create")))
    {
        report("xaudio2_8", "load", "FAIL", "xaudio2_8.dll or XAudio2Create missing, error %lu", GetLastError());
        return;
    }
    GetModuleFileNameA(module, path, sizeof(path));
    report("xaudio2_8", "load", "PASS", "%s", path);
    report("xaudio2_8", "export-with-version-info", GetProcAddress(module, "XAudio2CreateWithVersionInfo")
            ? "PASS" : "FAIL", "XAudio2CreateWithVersionInfo");

    hr = create(&xa, 0, 1);
    report("xaudio2_8", "create", SUCCEEDED(hr) && xa ? "PASS" : "FAIL", "hr=%#lx", hr);
    if (FAILED(hr) || !xa) return;
    hr = ((xa2_mastering28_func)vtable_of(xa)[7])(xa, &voice, 2, 48000, 0, NULL, NULL, 0);
    report("xaudio2_8", "mastering-voice", SUCCEEDED(hr) ? "PASS" : "SKIP",
            "hr=%#lx (a failure needs an audio endpoint and is not a version result)", hr);
    if (SUCCEEDED(hr) && voice)
    {
        hr = ((HRESULT (WINAPI *)(void *))vtable_of(xa)[8])(xa);
        report("xaudio2_8", "start-engine", SUCCEEDED(hr) ? "PASS" : "FAIL", "hr=%#lx", hr);
        ((void (WINAPI *)(void *))vtable_of(xa)[9])(xa);
        ((void (WINAPI *)(void *))vtable_of(voice)[21])(voice);
    }
    ((release_func)vtable_of(xa)[2])(xa);
}

static void test_x3daudio_and_xapofx(void)
{
    HMODULE x3d = LoadLibraryA("x3daudio1_7.dll"), fx = LoadLibraryA("xapofx1_5.dll");
    x3d_init_func init;
    BYTE handle[20];
    HRESULT hr;

    if (!x3d || !(init = (x3d_init_func)GetProcAddress(x3d, "X3DAudioInitialize")))
        report("x3daudio1_7", "load", "FAIL", "x3daudio1_7.dll or X3DAudioInitialize missing, error %lu",
                GetLastError());
    else
    {
        memset(handle, 0, sizeof(handle));
        hr = init(0x3, 343.5f, handle);
        report("x3daudio1_7", "initialize", SUCCEEDED(hr) ? "PASS" : "FAIL", "hr=%#lx", hr);
        report("x3daudio1_7", "calculate-export", GetProcAddress(x3d, "X3DAudioCalculate") ? "PASS" : "FAIL",
                "X3DAudioCalculate");
    }
    report("xapofx1_5", "load", fx && GetProcAddress(fx, "CreateFX") ? "PASS" : "FAIL", "CreateFX export");
}

int main(void)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("ITEM7-SUITE v%d begin\n", SUITE_VERSION);
    test_xaudio27();
    test_xaudio28();
    test_x3daudio_and_xapofx();
    printf("ITEM7-SUITE v%d end pass=%d fail=%d skip=%d\n", SUITE_VERSION, pass_count, fail_count, skip_count);
    return fail_count ? 1 : 0;
}
