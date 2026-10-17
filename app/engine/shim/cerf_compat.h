// Force-included into CERF sources (-include): MSVC CRT functions that some files use without including windows.h.
#pragma once
#include <time.h>
#include <cerrno>
static inline int localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : errno; }

// MSVC CRT: wide-path fopen_s (CERF's compact-flash image builders). Paths are narrowed to UTF-8.
#include <cstdio>
#include <string>
#include <windows.h>
static inline int _wfopen_s(FILE** out, const wchar_t* path, const wchar_t* mode) {
    char p[MAX_PATH], m[16];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, p, sizeof p, nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, mode, -1, m, sizeof m, nullptr, nullptr);
    *out = fopen(p, m);
    return *out ? 0 : errno;
}

// MSVC bounded printf as used by the net code: truncate, always terminate.
#ifndef _snprintf_s
#define _snprintf_s(buf, size, count, ...) snprintf(buf, size, __VA_ARGS__)
#endif
