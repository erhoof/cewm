
#include <windows.h>

#undef GetVersionEx

extern "C" BOOL WINAPI GetVersionEx(LPOSVERSIONINFOW);

extern "C" BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW lpvi) {
    return GetVersionEx(lpvi);
}
