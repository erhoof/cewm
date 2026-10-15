#ifndef CERF_NDIS_PACKET_RX_H
#define CERF_NDIS_PACKET_RX_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL CerfNdisPacketRxInit(void* adapter);
BOOL CerfNdisPacketRxReady(void);
BOOL CerfNdisPacketRxIndicate(const UCHAR* frame, ULONG len);
void CerfNdisPacketRxReturn(void* packet);
BOOL CerfNdisPacketRxShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
