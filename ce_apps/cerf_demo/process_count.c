#include "cerf_demo.h"

#include <tlhelp32.h>

typedef HANDLE (WINAPI *PFN_CreateSnap)(DWORD, DWORD);
typedef BOOL   (WINAPI *PFN_Entry32)(HANDLE, LPVOID);
typedef BOOL   (WINAPI *PFN_CloseSnap)(HANDLE);

static int CountSnapshot(DWORD flags, const wchar_t* firstFn,
                         const wchar_t* nextFn, DWORD entrySize) {
    HMODULE th = LoadLibraryW(L"toolhelp.dll");
    PFN_CreateSnap create;
    PFN_Entry32    first, next;
    PFN_CloseSnap  closesnap;
    HANDLE         snap;
    union {
        PROCESSENTRY32 pe;
        THREADENTRY32  te;
    } entry;
    int            n = 0;

    if (!th) return 0;
    create    = (PFN_CreateSnap)GetProcAddressW(th, L"CreateToolhelp32Snapshot");
    first     = (PFN_Entry32)GetProcAddressW(th, firstFn);
    next      = (PFN_Entry32)GetProcAddressW(th, nextFn);
    closesnap = (PFN_CloseSnap)GetProcAddressW(th, L"CloseToolhelp32Snapshot");
    if (!create || !first || !next) {
        FreeLibrary(th);
        return 0;
    }
    snap = create(flags, 0);
    if (snap == INVALID_HANDLE_VALUE) { FreeLibrary(th); return 0; }

    *(DWORD*)&entry = entrySize;
    if (first(snap, &entry)) {
        do { n++; *(DWORD*)&entry = entrySize; } while (next(snap, &entry));
    }
    if (closesnap) closesnap(snap);
    FreeLibrary(th);
    return n;
}

int CountProcesses(void) {
    return CountSnapshot(TH32CS_SNAPPROCESS, L"Process32First",
                         L"Process32Next", sizeof(PROCESSENTRY32));
}

int CountThreads(void) {
    return CountSnapshot(TH32CS_SNAPTHREAD, L"Thread32First",
                         L"Thread32Next", sizeof(THREADENTRY32));
}
