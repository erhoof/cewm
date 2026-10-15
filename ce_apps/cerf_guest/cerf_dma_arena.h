#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void  CerfArenaProcessAttach(void);
BOOL  CerfArenaEnter(void);
void  CerfArenaLeave(void);
void* CerfArenaAlloc(ULONG bytes, ULONG* out_offset);

#ifdef __cplusplus
}
#endif
