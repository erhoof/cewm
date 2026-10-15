
#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PFNVOID)(void);

BOOL VirtualCopy(LPVOID lpvDest, LPVOID lpvSrc, DWORD cbSize, DWORD fdwProtect);

HANDLE CreateAPISet(char acName[4], USHORT cFunctions, const PFNVOID *ppfnMethods,
                    const ULONGLONG *pu64Sig);
BOOL   RegisterAPISet(HANDLE hASet, DWORD dwSetID);
HANDLE CreateAPIHandle(HANDLE hASet, LPVOID pvData);

#define REGISTER_APISET_TYPE 0x80000000

#define ARG_TYPE_MASK 0x0f
#define ARG_TYPE_BITS 4

#define ARG_O_BIT 0x8
#define ARG_I_BIT 0x4

#define ARG_DW      0
#define ARG_I_PTR   (ARG_I_BIT)
#define ARG_I_WSTR  (ARG_I_BIT | 1)
#define ARG_I_ASTR  (ARG_I_BIT | 2)
#define ARG_I_PDW   (ARG_I_BIT | 3)
#define ARG_O_PTR   (ARG_O_BIT)
#define ARG_O_PDW   (ARG_O_BIT | 1)
#define ARG_O_PI64  (ARG_O_BIT | 2)
#define ARG_IO_PTR  (ARG_I_BIT | ARG_O_BIT)
#define ARG_IO_PDW  (ARG_IO_PTR | 1)
#define ARG_IO_PI64 (ARG_IO_PTR | 2)
#define ARG_CPTR    ARG_IO_PDW
#define ARG_PTR     ARG_IO_PDW

#define _ARG(arg, inx) ((ULONGLONG)ARG_##arg << (ARG_TYPE_BITS * ((inx) + 1)))

#define FNSIG0()   0
#define FNSIG1(a0) (_ARG(a0,0) | 1)
#define FNSIG2(a0,a1) (_ARG(a0,0)|_ARG(a1,1)|2)
#define FNSIG3(a0,a1,a2) (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|3)
#define FNSIG4(a0,a1,a2,a3) (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|4)
#define FNSIG5(a0,a1,a2,a3,a4) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|5)
#define FNSIG6(a0,a1,a2,a3,a4,a5) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|_ARG(a5,5)|6)
#define FNSIG7(a0,a1,a2,a3,a4,a5,a6) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|_ARG(a5,5)|_ARG(a6,6)|7)
#define FNSIG8(a0,a1,a2,a3,a4,a5,a6,a7) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|_ARG(a5,5)|_ARG(a6,6) \
    |_ARG(a7,7)|8)
#define FNSIG9(a0,a1,a2,a3,a4,a5,a6,a7,a8) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|_ARG(a5,5)|_ARG(a6,6) \
    |_ARG(a7,7)|_ARG(a8,8)|9)
#define FNSIG10(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|_ARG(a5,5)|_ARG(a6,6) \
    |_ARG(a7,7)|_ARG(a8,8)|_ARG(a9,9)|10)
#define FNSIG11(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|_ARG(a5,5)|_ARG(a6,6) \
    |_ARG(a7,7)|_ARG(a8,8)|_ARG(a9,9)|_ARG(a10,10)|11)
#define FNSIG12(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|_ARG(a5,5)|_ARG(a6,6) \
    |_ARG(a7,7)|_ARG(a8,8)|_ARG(a9,9)|_ARG(a10,10)|_ARG(a11,11)|12)
#define FNSIG13(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12) \
    (_ARG(a0,0)|_ARG(a1,1)|_ARG(a2,2)|_ARG(a3,3)|_ARG(a4,4)|_ARG(a5,5)|_ARG(a6,6) \
    |_ARG(a7,7)|_ARG(a8,8)|_ARG(a9,9)|_ARG(a10,10)|_ARG(a11,11)|_ARG(a12,12)|13)

#ifdef __cplusplus
}
#endif
