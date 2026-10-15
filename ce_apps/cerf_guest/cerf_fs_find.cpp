#include "cerf_fs_driver.h"

#include <windows.h>

static void HostFindClose(unsigned long tid) {
    CerfFsServerPB* pb = CerfFsPb();
    pb->fFindTransactionID = tid;
    CerfFsCall(pb, CERF_FS_OP_FIND_CLOSE);
}

static void FillFindData(WIN32_FIND_DATAW* fd, CerfFsServerPB* pb) {
    int n;
    memset(fd, 0, sizeof(*fd));
    fd->dwFileAttributes = pb->fFileAttributes;
    CerfFsDosToFiletime(pb->fFileCreateTimeDate, &fd->ftCreationTime);
    CerfFsDosToFiletime(pb->fFileTimeDate, &fd->ftLastWriteTime);
    fd->ftLastAccessTime = fd->ftLastWriteTime;
    fd->nFileSizeLow = pb->fSize;
    /* devemu_wm5 nk.exe 0x8009A12C OpenExe: oe.bIsOID =
       GetFileInformationByHandle(hf, &bhfi) && bhfi.dwOID != 0xFFFFFFFF. */
    fd->dwOID = (DWORD)-1;
    n = pb->u.fNameLength / sizeof(WCHAR);
    if (n > MAX_PATH - 1) n = MAX_PATH - 1;
    memcpy(fd->cFileName, pb->u.fName, n * sizeof(WCHAR));
    fd->cFileName[n] = 0;
}

HANDLE CerfFsFindFirstFileW(CerfVol* vol, HANDLE hProc, PCWSTR spec,
                            PWIN32_FIND_DATAW fd) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    HANDLE h = INVALID_HANDLE_VALUE;
    CerfFind* sc;
    int n;
    (void)vol;

    sc = (CerfFind*)LocalAlloc(LPTR, sizeof(CerfFind));
    if (!sc) {
        CerfFsResultToBool(ERROR_OUTOFMEMORY);
        return INVALID_HANDLE_VALUE;
    }
    sc->tid = (unsigned long)sc;

    CerfFsLock();
    pb->fIndex = 0;
    pb->fFindTransactionID = sc->tid;
    n = lstrlenW(spec);
    if (n > (int)CERF_FS_MAX_LFN) n = (int)CERF_FS_MAX_LFN;
    memcpy(pb->u.fName, spec, n * sizeof(WCHAR));
    pb->u.fName[n] = 0;
    pb->u.fNameLength = (unsigned short)(n * sizeof(WCHAR));

    e = CerfFsCall(pb, CERF_FS_OP_GET_INFO);
    if (e == CERF_FS_OK) {
        FillFindData(fd, pb);
        h = CerfFsMakeHandle(CerfFsFindApi(), sc, hProc);
        if (h == INVALID_HANDLE_VALUE) {
            HostFindClose(sc->tid);
            e = ERROR_OUTOFMEMORY;
        }
    }
    CerfFsUnlock();
    if (h == INVALID_HANDLE_VALUE) {
        LocalFree(sc);
        CerfFsResultToBool(e);
    }
    return h;
}

BOOL CerfFsFindNextFileW(CerfFind* s, PWIN32_FIND_DATAW fd) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    CerfFsLock();
    pb->fIndex = 1;
    pb->fFindTransactionID = s->tid;
    e = CerfFsCall(pb, CERF_FS_OP_GET_INFO);
    if (e == CERF_FS_OK) FillFindData(fd, pb);
    CerfFsUnlock();
    return CerfFsResultToBool(e);
}

BOOL CerfFsFindClose(CerfFind* s) {
    CerfFsLock();
    HostFindClose(s->tid);
    CerfFsUnlock();
    LocalFree(s);
    return TRUE;
}
