// Minimal Win32 subset over POSIX so CERF's hardware code (cerf/cerf, MIT) compiles on Linux without edits.
// Only what the non-host dirs use (checked by grep); add more when the compiler asks. Not a general Win32 layer.
#pragma once
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

typedef uint32_t DWORD;
typedef int BOOL;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef void* HANDLE;
typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef const wchar_t* LPCWSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef int64_t LONGLONG;
typedef int32_t LONG;
typedef int64_t LONG64;
typedef uint64_t ULONGLONG;
typedef uint64_t ULONG_PTR;
typedef uint64_t SIZE_T;
typedef union _LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
#define TRUE 1
#define FALSE 0
#ifndef NULL
#define NULL nullptr
#endif
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INFINITE 0xFFFFFFFFu
#define MAX_PATH 4096
#define CP_UTF8 65001
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xFFFFFFFFu
#define WINAPI
#define CALLBACK

static inline DWORD GetLastError() { return (DWORD)errno; }
static inline void SetLastError(DWORD e) { errno = (int)e; }
static inline void Sleep(DWORD ms) { usleep(ms * 1000); }
static inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }
static inline ULONGLONG GetTickCount64() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (ULONGLONG)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static inline DWORD GetTickCount() { return (DWORD)GetTickCount64(); }
static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f) { f->QuadPart = 1000000000; return TRUE; }
static inline BOOL QueryPerformanceCounter(LARGE_INTEGER* c) {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    c->QuadPart = (LONGLONG)t.tv_sec * 1000000000 + t.tv_nsec; return TRUE;
}

// UTF-8 <-> wchar_t (UTF-32 on Linux; CERF only round-trips paths and log text through these).
static inline int WideCharToMultiByte(UINT, DWORD, const wchar_t* w, int wn, char* out, int on, const char*, BOOL*) {
    std::string s;
    for (int i = 0; (wn < 0 ? w[i] != 0 : i < wn); i++) {
        uint32_t c = (uint32_t)w[i];
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | c >> 6); s += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { s += (char)(0xE0 | c >> 12); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
        else { s += (char)(0xF0 | c >> 18); s += (char)(0x80 | ((c >> 12) & 63)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
    }
    if (wn < 0) s += '\0';
    if (on > 0) memcpy(out, s.data(), (size_t)on < s.size() ? (size_t)on : s.size());
    return (int)s.size();
}
static inline int MultiByteToWideChar(UINT, DWORD, const char* u, int un, wchar_t* out, int on) {
    std::wstring w;
    size_t n = un < 0 ? strlen(u) + 1 : (size_t)un;
    for (size_t i = 0; i < n;) {
        uint8_t b = (uint8_t)u[i]; uint32_t c; int extra;
        if (b < 0x80) { c = b; extra = 0; } else if (b < 0xE0) { c = b & 31; extra = 1; }
        else if (b < 0xF0) { c = b & 15; extra = 2; } else { c = b & 7; extra = 3; }
        for (int k = 1; k <= extra && i + k < n; k++) c = (c << 6) | ((uint8_t)u[i + k] & 63);
        i += 1 + extra; w += (wchar_t)c;
    }
    if (on > 0) memcpy(out, w.data(), ((size_t)on < w.size() ? (size_t)on : w.size()) * sizeof(wchar_t));
    return (int)w.size();
}

// Critical sections -> recursive pthread mutex.
typedef struct _CRITICAL_SECTION { pthread_mutex_t m; } CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION* c) {
    pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&c->m, &a);
}
static inline void DeleteCriticalSection(CRITICAL_SECTION* c) { pthread_mutex_destroy(&c->m); }
static inline void EnterCriticalSection(CRITICAL_SECTION* c) { pthread_mutex_lock(&c->m); }
static inline void LeaveCriticalSection(CRITICAL_SECTION* c) { pthread_mutex_unlock(&c->m); }
static inline BOOL TryEnterCriticalSection(CRITICAL_SECTION* c) { return pthread_mutex_trylock(&c->m) == 0; }

// Auto-reset events -> eventfd (semaphore-less: one wake per signal).
static inline HANDLE CreateEventW(void*, BOOL, BOOL initial, LPCWSTR) {
    return (HANDLE)(intptr_t)eventfd(initial ? 1 : 0, EFD_CLOEXEC);
}
static inline BOOL SetEvent(HANDLE h) { uint64_t one = 1; return write((int)(intptr_t)h, &one, 8) == 8; }
static inline BOOL CloseHandle(HANDLE h) { return close((int)(intptr_t)h) == 0; }

// ---- additions (round 2) ----
typedef void* HWND;
typedef void* HDC;
typedef void* HMODULE;
#define PAGE_EXECUTE_READ 0x20
#define MB_OK 0
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
static inline int lstrlenW(const wchar_t* s) { return (int)wcslen(s); }
static inline void YieldProcessor() { __asm__ __volatile__("" ::: "memory"); }
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#include <strings.h>
static inline DWORD GetModuleFileNameW(HMODULE, wchar_t* out, DWORD n) {
    // CEWM_DATA_DIR relocates CERF's config/devices dir (an installed binary lives in /usr/bin, its data in /usr/share/<pkg>).
    char buf[MAX_PATH]; ssize_t r;
    if (const char* dir = getenv("CEWM_DATA_DIR")) r = snprintf(buf, sizeof buf, "%s/cewm", dir);
    else r = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (r <= 0) return 0;
    DWORD i = 0; for (; i < (DWORD)r && i + 1 < n; i++) out[i] = (unsigned char)buf[i];
    out[i] = 0; return i;
}
// VirtualAlloc: lazily-backed anonymous mapping (CERF's guest RAM relies on first-touch commit).
static inline LPVOID VirtualAlloc(LPVOID addr, SIZE_T size, DWORD, DWORD prot) {
    int p = prot == PAGE_NOACCESS ? PROT_NONE : prot == PAGE_READONLY ? PROT_READ : PROT_READ | PROT_WRITE;
    void* r = mmap(addr, size, p, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (addr ? MAP_FIXED : 0), -1, 0);
    return r == MAP_FAILED ? nullptr : r;
}
static inline BOOL VirtualFree(LPVOID addr, SIZE_T size, DWORD) { return size ? munmap(addr, size) == 0 : TRUE; }

// ---- additions (round 3): types only, so headers that mention UI types parse ----
typedef uint64_t WPARAM;
typedef int64_t LPARAM;
typedef uint32_t COLORREF;
typedef void* HFONT;
typedef void* HKEY;
struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };
struct WINDOWPLACEMENT { UINT length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; };
#define MB_ICONERROR 0x10
#define MB_ICONWARNING 0x30
#define MB_ICONINFORMATION 0x40
#define _TRUNCATE ((size_t)-1)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2

// ---- additions (round 4) ----
typedef void* HMENU;
typedef void* HBITMAP;
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define KEY_QUERY_VALUE 1
#define TIMER_ALL_ACCESS 0
static inline wchar_t* lstrcpynW(wchar_t* d, const wchar_t* s, int n) {
    if (n <= 0) return d; wcsncpy(d, s, n - 1); d[n - 1] = 0; return d;
}
static inline BOOL WriteFile(HANDLE h, LPCVOID b, DWORD n, DWORD* written, void*) {
    ssize_t r = write((int)(intptr_t)h, b, n); if (written) *written = r < 0 ? 0 : (DWORD)r; return r >= 0;
}
#define __assume(x) ((void)0)

// ---- additions (round 5): UI types only ----
typedef int64_t LRESULT;
typedef void* HICON;
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | ((DWORD)((BYTE)(b)) << 16)))
typedef uint64_t DWORD_PTR;
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x100
#define FORMAT_MESSAGE_FROM_SYSTEM 0x1000
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x200
// shortcut: diagnostic text only; WindowsErrorText() falls back to its generic message when this returns 0.
static inline DWORD FormatMessageW(DWORD, LPCVOID, DWORD, DWORD, LPWSTR, DWORD, void*) { return 0; }

#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFF)
static inline DWORD GetFileAttributesW(LPCWSTR p) {  // log-only use in FolderShareConfig: exists? dir?
    std::string u; for (; *p; p++) u += (char)*p;      // host paths are ASCII-safe enough for a log line
    struct stat st; return stat(u.c_str(), &st) ? INVALID_FILE_ATTRIBUTES : (S_ISDIR(st.st_mode) ? 0x10 : 0x20);
}
