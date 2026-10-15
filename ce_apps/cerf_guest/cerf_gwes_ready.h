#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

DWORD CerfShWmgrApiSet(void);
BOOL  CerfIsApiReadyAvailable(void);
BOOL  CerfGwesApiSetReady(void);
BOOL  CerfWaitGwesApiSet(void);

#ifdef __cplusplus
}
#endif
