#include <windows.h>

#include "cerf_process_spawn.h"

static BOOL CerfSpawn(const WCHAR* image, const WCHAR* args, DWORD* err) {
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(image, args, NULL, NULL, FALSE, 0, NULL, NULL, NULL,
                        &pi)) {
        *err = GetLastError();
        return FALSE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

extern "C" BOOL CerfSpawnCommandLine(WCHAR* cmd, DWORD* err) {
    WCHAR* image = cmd;
    WCHAR* args  = NULL;
    WCHAR* p;
    BOOL   ok;

    *err = 0;
    if (cmd[0] == L'"') {
        image = cmd + 1;
        for (p = image; *p && *p != L'"'; ++p) {}
        if (*p) {
            *p++ = 0;
            while (*p == L' ') ++p;
            if (*p) args = p;
        }
        return CerfSpawn(image, args, err);
    }

    ok = CerfSpawn(image, NULL, err);
    if (ok) return TRUE;
    for (p = image; *p; ++p) {
        if (*p == L' ') {
            *p   = 0;
            args = p + 1;
            break;
        }
    }
    if (!args) return FALSE;
    return CerfSpawn(image, args, err);
}
