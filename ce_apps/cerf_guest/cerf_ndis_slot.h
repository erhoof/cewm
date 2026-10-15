#ifndef CERF_NDIS_SLOT_H
#define CERF_NDIS_SLOT_H

#include <windows.h>

#include "cerf_ndis.h"

#define CERF_MP_ETH_HDR   14
#define CERF_MP_RX_SLOTS  16

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void*  packet;
    UCHAR* data;
    LONG   busy;
} CerfRxSlot;

typedef struct {
    DWORD            pid;

    CRITICAL_SECTION life_cs;
    BOOL             life_cs_ready;

    NDIS_HANDLE     wrapper;
    NDIS_HANDLE     adapter;
    volatile ULONG* nic;
    volatile ULONG* nic_page;
    volatile UCHAR* stage;
    UCHAR           mac[6];
    ULONG           filter;
    ULONG           lookahead;

    ULONG           rx_consumed;
    HANDLE          rx_thread;
    volatile LONG   rx_stop;
    UCHAR           rx_hdr[CERF_MP_ETH_HDR];
    UCHAR*          rx_body;
    ULONG           rx_body_len;

    ULONG           ethdb_off;
    ULONG           block_off;
    ULONG           os_major;
    ULONG           os_minor;
    int             layout_resolved;

    void*           pkt_adapter;
    void*           pkt_pool;
    void*           buf_pool;
    CerfRxSlot      pkt_slot[CERF_MP_RX_SLOTS];
    BOOL            pkt_ready;
    LONG            pkt_draining;
} CerfMpSlot;

CerfMpSlot* CerfMpSelf(void);
CerfMpSlot* CerfMpFind(void);

#ifdef __cplusplus
}
#endif

#endif
