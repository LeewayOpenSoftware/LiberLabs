#ifndef LIBERX_LEGACY_H
#define LIBERX_LEGACY_H

#include <windows.h>

#define LEGACY_VERSION 1

extern int fail_count, skip_count, pass_count;

/* status is "PASS", "FAIL" or "SKIP"; only the first letter is read. */
void report(const char *api, const char *name, const char *status, const char *fmt, ...);
void check(const char *api, const char *name, BOOL ok, const char *fmt, ...);

/* d3d8_render.c */
void run_d3d8(HWND hwnd);

#endif
