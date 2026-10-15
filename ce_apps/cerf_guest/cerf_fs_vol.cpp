#include "cerf_fs_driver.h"

#include <windows.h>

static void SetName(CerfFsServerPB* pb, PCWSTR name) {
    int n = lstrlenW(name);
    if (n > (int)CERF_FS_MAX_LFN) n = (int)CERF_FS_MAX_LFN;
    memcpy(pb->u.fName, name, n * sizeof(WCHAR));
    pb->u.fName[n] = 0;
    pb->u.fNameLength = (unsigned short)(n * sizeof(WCHAR));
}

BOOL CerfFsCreateDirectoryW(CerfVol* vol, PCWSTR path, LPSECURITY_ATTRIBUTES sa) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    (void)vol; (void)sa;
    CerfFsLock();
    SetName(pb, path);
    e = CerfFsCall(pb, CERF_FS_OP_MKDIR);
    CerfFsUnlock();
    return CerfFsResultToBool(e);
}

BOOL CerfFsRemoveDirectoryW(CerfVol* vol, PCWSTR path) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    (void)vol;
    CerfFsLock();
    SetName(pb, path);
    e = CerfFsCall(pb, CERF_FS_OP_RMDIR);
    CerfFsUnlock();
    return CerfFsResultToBool(e);
}

BOOL CerfFsDeleteFileW(CerfVol* vol, PCWSTR name) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    (void)vol;
    CerfFsLock();
    SetName(pb, name);
    e = CerfFsCall(pb, CERF_FS_OP_DELETE);
    CerfFsUnlock();
    return CerfFsResultToBool(e);
}

BOOL CerfFsMoveFileW(CerfVol* vol, PCWSTR oldn, PCWSTR newn) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    int n2;
    (void)vol;
    CerfFsLock();
    SetName(pb, oldn);
    n2 = lstrlenW(newn);
    if (n2 > (int)CERF_FS_MAX_LFN) n2 = (int)CERF_FS_MAX_LFN;
    memcpy(pb->u.fName2, newn, n2 * sizeof(WCHAR));
    pb->u.fName2[n2] = 0;
    pb->u.fName2Length = (unsigned short)(n2 * sizeof(WCHAR));
    e = CerfFsCall(pb, CERF_FS_OP_RENAME);
    CerfFsUnlock();
    return CerfFsResultToBool(e);
}

BOOL CerfFsDeleteAndRenameFileW(CerfVol* vol, PCWSTR oldn, PCWSTR newn) {
    if (CerfFsDeleteFileW(vol, oldn))
        return CerfFsMoveFileW(vol, newn, oldn);
    return FALSE;
}

DWORD CerfFsGetFileAttributesW(CerfVol* vol, PCWSTR name) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    DWORD r = (DWORD)-1;
    (void)vol;
    CerfFsLock();
    pb->fIndex = -1;
    pb->fFindTransactionID = 0xFFFFFFFFu;
    SetName(pb, name);
    e = CerfFsCall(pb, CERF_FS_OP_GET_INFO);
    if (e == CERF_FS_OK) r = pb->fFileAttributes;
    CerfFsUnlock();
    if (r == (DWORD)-1) CerfFsResultToBool(e);
    return r;
}

BOOL CerfFsSetFileAttributesW(CerfVol* vol, PCWSTR name, DWORD attrs) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    (void)vol;
    CerfFsLock();
    SetName(pb, name);
    pb->fFileAttributes = (unsigned short)attrs;
    pb->fFileTimeDate = 0;
    e = CerfFsCall(pb, CERF_FS_OP_SET_ATTRS);
    CerfFsUnlock();
    return CerfFsResultToBool(e);
}

BOOL CerfFsGetDiskFreeSpaceW(CerfVol* vol, PCWSTR path, PDWORD pSectorsPerCluster,
                             PDWORD pBytesPerSector, PDWORD pFreeClusters,
                             PDWORD pClusters) {
    CerfFsServerPB* pb = CerfFsPb();
    unsigned long e;
    (void)vol; (void)path;
    CerfFsLock();
    e = CerfFsCall(pb, CERF_FS_OP_GET_SPACE);
    if (e == CERF_FS_OK) {
        *pBytesPerSector    = 512;
        *pSectorsPerCluster = 64;
        *pFreeClusters      = pb->fSize;
        *pClusters          = pb->fPosition;
    }
    CerfFsUnlock();
    return CerfFsResultToBool(e);
}
