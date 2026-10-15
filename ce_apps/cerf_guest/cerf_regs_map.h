#ifndef CERF_REGS_MAP_H
#define CERF_REGS_MAP_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void* CerfMapRegsPage(ULONG pa, ULONG size);

#ifdef __cplusplus
}
#endif

#endif
