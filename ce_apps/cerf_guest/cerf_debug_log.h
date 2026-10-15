#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif
void CerfInitLogging(unsigned long id);
void CerfDebugTx(const char* msg);
void CerfDebugTxX(const char* msg, DWORD value);
void CerfDebugFatal(const char* msg);
#ifdef __cplusplus
}
#endif

enum {
    CERF_LOG_CH_STUB           = 0,
    CERF_LOG_CH_DISPLAY        = 1,
    CERF_LOG_CH_SHARED_FOLDERS = 2,
};

#define CERF_LOG(msg)        CerfDebugTx(msg)
#define CERF_LOG_X(msg, val) CerfDebugTxX((msg), (DWORD)(val))
#define CERF_LOG_INIT(id)    CerfInitLogging((unsigned long)(id))
#define CERF_FATAL(msg)      CerfDebugFatal(msg)

#if CERF_DEV_MODE
#define CERF_LOG_DEV(msg)        CerfDebugTx(msg)
#define CERF_LOG_X_DEV(msg, val) CerfDebugTxX((msg), (DWORD)(val))
#else
#define CERF_LOG_DEV(msg)        ((void)0)
#define CERF_LOG_X_DEV(msg, val) ((void)0)
#endif
