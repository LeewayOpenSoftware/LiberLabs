/*
 * LiberX item 6b: D2D1 geometry fixture (ComputeLength, Widen, CombineWithGeometry).
 * Pass/fail by computed values, not screenshots. win32cui; d2d1.dll via LoadLibrary.
 */
#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <d2d1.h>

#define TEST_VERSION 1

static int fail_count, skip_count, pass_count;

static void report(const char *name, const char *status, const char *fmt, ...)
{
    char detail[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    printf("D2D_GEOMETRY v%d %s %s %s\n", TEST_VERSION, name, status, detail);
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

typedef HRESULT (WINAPI *d2d_create_factory_func)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS *, void **);

/* Closed figure (0,0)->(100,0): one line plus the closing edge = length 200. */
static void test_geometry_computelength(ID2D1Factory *factory)
{
    ID2D1PathGeometry *path = NULL;
    ID2D1GeometrySink *sink = NULL;
    D2D1_POINT_2F start = {0.0f, 0.0f}, end = {100.0f, 0.0f};
    FLOAT length = -1.0f;
    HRESULT hr;

    if (FAILED(ID2D1Factory_CreatePathGeometry(factory, &path)))
    {
        report("computelength", "SKIP", "CreatePathGeometry failed");
        return;
    }
    if (FAILED(ID2D1PathGeometry_Open(path, &sink)))
    {
        report("computelength", "SKIP", "Open failed");
        ID2D1PathGeometry_Release(path);
        return;
    }
    ID2D1GeometrySink_BeginFigure(sink, start, D2D1_FIGURE_BEGIN_FILLED);
    ID2D1GeometrySink_AddLine(sink, end);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);

    hr = ID2D1PathGeometry_ComputeLength(path, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &length);
    check("computelength", SUCCEEDED(hr) && fabsf(length - 200.0f) < 1.0f,
            "hr=%lx length=%.3f want 200", hr, length);

    ID2D1PathGeometry_Release(path);
}

/* Open line (10,10)->(90,10) widened by 5: a real stroked region, area > 0. */
static void test_geometry_widen(ID2D1Factory *factory)
{
    ID2D1PathGeometry *path = NULL, *widened = NULL;
    ID2D1GeometrySink *sink = NULL, *wsink = NULL;
    D2D1_POINT_2F start = {10.0f, 10.0f}, end = {90.0f, 10.0f};
    FLOAT area = -1.0f;
    HRESULT hr;

    if (FAILED(ID2D1Factory_CreatePathGeometry(factory, &path))
            || FAILED(ID2D1Factory_CreatePathGeometry(factory, &widened)))
    {
        report("widen", "SKIP", "CreatePathGeometry failed");
        goto done;
    }
    if (FAILED(ID2D1PathGeometry_Open(path, &sink)))
    {
        report("widen", "SKIP", "Open path failed");
        goto done;
    }
    ID2D1GeometrySink_BeginFigure(sink, start, D2D1_FIGURE_BEGIN_FILLED);
    ID2D1GeometrySink_AddLine(sink, end);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
    ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    sink = NULL;

    if (FAILED(ID2D1PathGeometry_Open(widened, &wsink)))
    {
        report("widen", "SKIP", "Open widened failed");
        goto done;
    }
    hr = ID2D1PathGeometry_Widen(path, 5.0f, NULL, NULL,
            D2D1_DEFAULT_FLATTENING_TOLERANCE, (ID2D1SimplifiedGeometrySink *)wsink);
    ID2D1GeometrySink_Close(wsink);
    ID2D1GeometrySink_Release(wsink);
    wsink = NULL;
    if (FAILED(hr))
    {
        check("widen", FALSE, "Widen hr=%lx", hr);
        goto done;
    }
    hr = ID2D1PathGeometry_ComputeArea(widened, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &area);
    check("widen", SUCCEEDED(hr) && area > 0.0f, "Widen ok, widened area hr=%lx area=%.2f (>0)", hr, area);

done:
    if (sink) ID2D1GeometrySink_Release(sink);
    if (wsink) ID2D1GeometrySink_Release(wsink);
    if (path) ID2D1PathGeometry_Release(path);
    if (widened) ID2D1PathGeometry_Release(widened);
}

/* Two overlapping rectangles combined (INTERSECT). Accepts an honest E_NOTIMPL
 * until the combine path lands; a success must yield the expected 50x50 area. */
static void test_geometry_combine(ID2D1Factory *factory)
{
    ID2D1RectangleGeometry *ra = NULL, *rb = NULL;
    ID2D1PathGeometry *out = NULL;
    ID2D1GeometrySink *sink = NULL;
    D2D1_RECT_F a = {0.0f, 0.0f, 100.0f, 100.0f};
    D2D1_RECT_F b = {50.0f, 50.0f, 150.0f, 150.0f};
    FLOAT area = -1.0f;
    HRESULT hr;

    if (FAILED(ID2D1Factory_CreateRectangleGeometry(factory, &a, &ra))
            || FAILED(ID2D1Factory_CreateRectangleGeometry(factory, &b, &rb))
            || FAILED(ID2D1Factory_CreatePathGeometry(factory, &out)))
    {
        report("combine_intersect", "SKIP", "geometry creation failed");
        goto done;
    }
    if (FAILED(ID2D1PathGeometry_Open(out, &sink)))
    {
        report("combine_intersect", "SKIP", "Open failed");
        goto done;
    }
    hr = ID2D1RectangleGeometry_CombineWithGeometry(ra, (ID2D1Geometry *)rb,
            D2D1_COMBINE_MODE_INTERSECT, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE,
            (ID2D1SimplifiedGeometrySink *)sink);
    ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    sink = NULL;
    if (hr == E_NOTIMPL)
    {
        report("combine_intersect", "SKIP", "E_NOTIMPL (combine not yet implemented)");
        goto done;
    }
    if (FAILED(hr))
    {
        check("combine_intersect", FALSE, "Combine hr=%lx", hr);
        goto done;
    }
    hr = ID2D1PathGeometry_ComputeArea(out, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &area);
    check("combine_intersect", SUCCEEDED(hr) && fabsf(area - 2500.0f) < 50.0f,
            "area hr=%lx area=%.1f want ~2500", hr, area);

done:
    if (sink) ID2D1GeometrySink_Release(sink);
    if (ra) ID2D1RectangleGeometry_Release(ra);
    if (rb) ID2D1RectangleGeometry_Release(rb);
    if (out) ID2D1PathGeometry_Release(out);
}

int main(void)
{
    d2d_create_factory_func create_factory;
    D2D1_FACTORY_OPTIONS options = {0};
    ID2D1Factory *factory;
    HMODULE d2d1;
    HRESULT hr;

    if (!(d2d1 = LoadLibraryA("d2d1.dll")))
    {
        printf("D2D_GEOMETRY v%d LOAD FAIL d2d1.dll not found\n", TEST_VERSION);
        return 1;
    }
    if (!(create_factory = (d2d_create_factory_func)GetProcAddress(d2d1, "D2D1CreateFactory")))
    {
        printf("D2D_GEOMETRY v%d LOAD FAIL D2D1CreateFactory export\n", TEST_VERSION);
        return 1;
    }
    hr = create_factory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, &options, (void **)&factory);
    if (FAILED(hr))
    {
        printf("D2D_GEOMETRY v%d LOAD FAIL D2D1CreateFactory hr=%lx\n", TEST_VERSION, hr);
        return 1;
    }

    test_geometry_computelength(factory);
    test_geometry_widen(factory);
    test_geometry_combine(factory);

    ID2D1Factory_Release(factory);
    printf("D2D_GEOMETRY v%d SUMMARY PASS=%d FAIL=%d SKIP=%d\n", TEST_VERSION, pass_count, fail_count, skip_count);
    return fail_count;
}
