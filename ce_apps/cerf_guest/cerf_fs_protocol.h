#pragma once

#include <windows.h>

#define CERF_FS_MAX_IO       (64u * 1024u)
#define CERF_FS_MAX_LFN      255u

#define CERF_FS_OP_POLL          0x00u
#define CERF_FS_OP_CREATE        0x05u
#define CERF_FS_OP_OPEN          0x06u
#define CERF_FS_OP_READ          0x07u
#define CERF_FS_OP_WRITE         0x08u
#define CERF_FS_OP_SET_EOF       0x09u
#define CERF_FS_OP_CLOSE         0x0Au
#define CERF_FS_OP_GET_SPACE     0x0Bu
#define CERF_FS_OP_MKDIR         0x0Cu
#define CERF_FS_OP_RMDIR         0x0Du
#define CERF_FS_OP_SET_ATTRS     0x0Eu
#define CERF_FS_OP_RENAME        0x0Fu
#define CERF_FS_OP_DELETE        0x10u
#define CERF_FS_OP_GET_INFO      0x11u
#define CERF_FS_OP_GET_FCB_INFO  0x13u
#define CERF_FS_OP_FIND_CLOSE    0x16u

#define CERF_FS_OK               0x00u
#define CERF_FS_E_FILE_NOT_FOUND 0x02u
#define CERF_FS_E_GENERAL        0x1Fu
#define CERF_FS_E_DISK_FULL      0x27u

#define CERF_FS_ACCESS_READ      0x00u
#define CERF_FS_ACCESS_WRITE     0x01u
#define CERF_FS_ACCESS_RW        0x02u
#define CERF_FS_SHARE_DENY_RW    0x10u
#define CERF_FS_SHARE_DENY_WRITE 0x20u
#define CERF_FS_SHARE_DENY_READ  0x30u
#define CERF_FS_SHARE_DENY_NONE  0x40u
#define CERF_FS_SHARE_MASK       0x70u

#pragma pack(push, 4)

typedef struct CerfFsServerPB {
    unsigned short fStructureSize;
    unsigned short fResult;
    unsigned long  fFindTransactionID;
    short          fIndex;
    unsigned short fHandle;
    unsigned long  fFileTimeDate;
    unsigned long  fSize;
    unsigned long  fPosition;
    unsigned long  fDTAPtr;
    unsigned short fFileAttributes;
    unsigned short fOpenMode;
    unsigned char  fWildCard;
    struct {
        unsigned short fNameLength;
        unsigned short fName[CERF_FS_MAX_LFN + 1];
        unsigned short fName2Length;
        unsigned short fName2[CERF_FS_MAX_LFN + 1];
    } u;
    unsigned long  fFileCreateTimeDate;
} CerfFsServerPB;

typedef struct CerfFsChannel {
    volatile unsigned long  ServerPB;
    volatile unsigned long  Code;
    volatile unsigned long  IOPending;
    volatile unsigned long  Result;
    volatile unsigned long  Enabled;
    volatile unsigned long  Generation;
    volatile unsigned long  _pad18;
    volatile unsigned long  _pad1c;
    volatile unsigned short MountPoint[64];
} CerfFsChannel;

#pragma pack(pop)
