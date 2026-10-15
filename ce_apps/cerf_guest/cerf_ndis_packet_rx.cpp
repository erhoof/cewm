#include "cerf_ndis.h"
#include "cerf_ndis_miniport.h"
#include "cerf_ndis_packet_rx.h"
#include "cerf_ndis_slot.h"
#include "cerf_debug_log.h"

#include "../../cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"
#include "../../cerf/peripherals/cerf_virt/cerf_virt_nic_regs.h"

#define CERF_MP_RX_SLOT_BYTES (CerfVirt::kNicSlotSize - CerfVirt::kNicSlotPayloadOff)

#define CERF_NDIS_PACKET_TAIL 12

#define CERF_RX_DRAIN_IDLE    0
#define CERF_RX_DRAIN_PENDING 1
#define CERF_RX_DRAIN_FREEING 2

typedef void (*PFN_PacketIndicate)(void* adapter, void** packets, UINT count);

static UCHAR* CerfRxOob(void* packet) {
    return (UCHAR*)packet +
           *(USHORT*)((UCHAR*)packet + CERF_NDIS_PACKET_OOB_OFF);
}

static void CerfRxReclaimSlot(const CerfNdisApi* api, CerfMpSlot* mp, int i) {
    void* packet = mp->pkt_slot[i].packet;
    void* buf;

    if (!packet) return;
    buf = *(void**)((UCHAR*)packet + CERF_NDIS_PACKET_HEAD);
    if (buf && api && api->FreeBuffer) api->FreeBuffer(buf);
    *(void**)((UCHAR*)packet + CERF_NDIS_PACKET_HEAD) = 0;
    *(void**)((UCHAR*)packet + CERF_NDIS_PACKET_TAIL) = 0;
    *(ULONG*)(CerfRxOob(packet) + CERF_NDIS_OOB_STATUS) = 0;
    InterlockedExchange(&mp->pkt_slot[i].busy, 0);
}

static PFN_PacketIndicate CerfRxIndicateHandler(CerfMpSlot* mp) {
    ULONG ethdb = CerfMpEthDbOff();

    if (!mp->pkt_adapter || !ethdb) return 0;
    return *(PFN_PacketIndicate*)((UCHAR*)mp->pkt_adapter + ethdb +
                                  CERF_NDIS_BLOCK_PKTINDICATE_FROM_ETHDB);
}

BOOL CerfNdisPacketRxInit(void* adapter) {
    const CerfNdisApi* api = CerfNdisResolve();
    CerfMpSlot* mp = CerfMpSelf();
    NDIS_STATUS st = CERF_NDIS_STATUS_FAILURE;
    int i;

    if (!api || !adapter) return FALSE;
    if (mp->pkt_draining) {
        CERF_LOG("ndis: rebind blocked, the previous pools are still held by NDIS");
        return FALSE;
    }
    if (mp->pkt_ready) {
        if (mp->pkt_adapter == adapter) return TRUE;
        CERF_LOG_X("ndis: packet rx rebinding, superseded adapter",
                   (ULONG)mp->pkt_adapter);
        if (!CerfNdisPacketRxShutdown()) {
            CERF_LOG("ndis: rebind blocked, NDIS still holds the old packets");
            return FALSE;
        }
    }
    if (!api->AllocatePacketPool || !api->AllocatePacket ||
        !api->AllocateBufferPool || !api->AllocateBuffer) {
        CERF_LOG("ndis: packet pools unavailable - legacy receive only");
        return FALSE;
    }

    if (CerfMpOsMajor() < 7) {
        CERF_LOG_X("ndis: legacy receive path for os major", CerfMpOsMajor());
        return FALSE;
    }

    mp->pkt_adapter = adapter;

    api->AllocatePacketPool(&st, &mp->pkt_pool, CERF_MP_RX_SLOTS, 0);
    if (st != CERF_NDIS_STATUS_SUCCESS || !mp->pkt_pool) {
        CERF_LOG_X("ndis: packet pool alloc failed", (ULONG)st);
        mp->pkt_adapter = 0;
        return FALSE;
    }
    api->AllocateBufferPool(&st, &mp->buf_pool, CERF_MP_RX_SLOTS);
    if (st != CERF_NDIS_STATUS_SUCCESS) {
        CERF_LOG_X("ndis: buffer pool alloc failed", (ULONG)st);
        if (api->FreePacketPool) api->FreePacketPool(mp->pkt_pool);
        mp->pkt_pool    = 0;
        mp->buf_pool    = 0;
        mp->pkt_adapter = 0;
        return FALSE;
    }

    for (i = 0; i < CERF_MP_RX_SLOTS; ++i) {
        mp->pkt_slot[i].data = (UCHAR*)LocalAlloc(LMEM_FIXED, CERF_MP_RX_SLOT_BYTES);
        if (!mp->pkt_slot[i].data) break;
        api->AllocatePacket(&st, &mp->pkt_slot[i].packet, mp->pkt_pool);
        if (st != CERF_NDIS_STATUS_SUCCESS || !mp->pkt_slot[i].packet) break;
        mp->pkt_slot[i].busy = 0;
    }
    if (i == 0) {
        CERF_LOG("ndis: no receive packets allocated");
        if (mp->pkt_slot[0].data) {
            LocalFree(mp->pkt_slot[0].data);
            mp->pkt_slot[0].data = 0;
        }
        if (api->FreePacketPool) api->FreePacketPool(mp->pkt_pool);
        if (api->FreeBufferPool) api->FreeBufferPool(mp->buf_pool);
        mp->pkt_pool    = 0;
        mp->buf_pool    = 0;
        mp->pkt_adapter = 0;
        return FALSE;
    }

    mp->pkt_ready = TRUE;
    CERF_LOG_X("ndis: packet receive ready, slots", (ULONG)i);
    return TRUE;
}

BOOL CerfNdisPacketRxReady(void) { return CerfMpSelf()->pkt_ready; }

BOOL CerfNdisPacketRxIndicate(const UCHAR* frame, ULONG len) {
    const CerfNdisApi* api = CerfNdisResolve();
    CerfMpSlot* mp = CerfMpSelf();
    PFN_PacketIndicate indicate;
    NDIS_STATUS st = CERF_NDIS_STATUS_FAILURE;
    void* buf = 0;
    void* one[1];
    int i;

    if (!mp->pkt_ready || !api || !frame) return FALSE;
    if (len == 0 || len > CERF_MP_RX_SLOT_BYTES) {
        CERF_LOG_X("ndis: packet rx rejected, len out of range", len);
        return FALSE;
    }

    indicate = CerfRxIndicateHandler(mp);
    if (!indicate) {
        CERF_LOG("ndis: packet rx has no indicate handler on the block");
        return FALSE;
    }

    for (i = 0; i < CERF_MP_RX_SLOTS; ++i) {
        if (mp->pkt_slot[i].packet &&
            InterlockedExchange(&mp->pkt_slot[i].busy, 1) == 0)
            break;
    }
    if (i == CERF_MP_RX_SLOTS) {
        CERF_LOG("ndis: packet rx table exhausted - all slots still held by NDIS");
        return FALSE;
    }

    memcpy(mp->pkt_slot[i].data, frame, len);
    api->AllocateBuffer(&st, &buf, mp->buf_pool, mp->pkt_slot[i].data, len);
    if (st != CERF_NDIS_STATUS_SUCCESS || !buf) {
        CERF_LOG_X("ndis: packet rx buffer alloc failed", (ULONG)st);
        InterlockedExchange(&mp->pkt_slot[i].busy, 0);
        return FALSE;
    }

    {
        UCHAR* p = (UCHAR*)mp->pkt_slot[i].packet;
        *(void**)(p + CERF_NDIS_PACKET_HEAD)      = buf;
        *(void**)(p + CERF_NDIS_PACKET_TAIL)      = buf;
        *(ULONG*)(p + CERF_NDIS_PACKET_TOTALLEN)  = len;
        *(ULONG*)(p + CERF_NDIS_PACKET_COUNT)     = 1;
        *(ULONG*)(p + CERF_NDIS_PACKET_PHYSCOUNT) = 0;
        *(p + CERF_NDIS_PACKET_VALIDCOUNTS)       = 1;
    }

    *(ULONG*)(CerfRxOob(mp->pkt_slot[i].packet) + CERF_NDIS_OOB_STATUS) = 0;
    one[0] = mp->pkt_slot[i].packet;
    indicate(mp->pkt_adapter, one, 1);

    {
        ULONG oob_status =
            *(ULONG*)(CerfRxOob(mp->pkt_slot[i].packet) + CERF_NDIS_OOB_STATUS);
        if (oob_status == 0)
            CerfRxReclaimSlot(api, mp, i);
    }
    return TRUE;
}

static void CerfRxFreePools(const CerfNdisApi* api, CerfMpSlot* mp) {
    int i;

    for (i = 0; i < CERF_MP_RX_SLOTS; ++i) {
        if (mp->pkt_slot[i].packet) {
            void* buf = *(void**)((UCHAR*)mp->pkt_slot[i].packet +
                                  CERF_NDIS_PACKET_HEAD);
            if (buf && api && api->FreeBuffer) api->FreeBuffer(buf);
            if (api && api->FreePacket) api->FreePacket(mp->pkt_slot[i].packet);
            mp->pkt_slot[i].packet = 0;
        }
        if (mp->pkt_slot[i].data) {
            LocalFree(mp->pkt_slot[i].data);
            mp->pkt_slot[i].data = 0;
        }
    }

    if (api && api->FreePacketPool && mp->pkt_pool) api->FreePacketPool(mp->pkt_pool);
    if (api && api->FreeBufferPool && mp->buf_pool) api->FreeBufferPool(mp->buf_pool);
    mp->pkt_pool = 0;
    mp->buf_pool = 0;
}

static BOOL CerfRxTryFreePools(const CerfNdisApi* api, CerfMpSlot* mp) {
    int i;

    if (mp->pkt_draining != CERF_RX_DRAIN_PENDING) return FALSE;
    for (i = 0; i < CERF_MP_RX_SLOTS; ++i)
        if (mp->pkt_slot[i].busy) return FALSE;
    if (InterlockedExchange(&mp->pkt_draining, CERF_RX_DRAIN_FREEING) !=
        CERF_RX_DRAIN_PENDING)
        return FALSE;

    CerfRxFreePools(api, mp);
    InterlockedExchange(&mp->pkt_draining, CERF_RX_DRAIN_IDLE);
    return TRUE;
}

void CerfNdisPacketRxReturn(void* packet) {
    const CerfNdisApi* api = CerfNdisResolve();
    CerfMpSlot* mp = CerfMpSelf();
    int i;

    if (!packet) return;

    for (i = 0; i < CERF_MP_RX_SLOTS; ++i) {
        if (mp->pkt_slot[i].packet == packet) break;
    }
    if (i == CERF_MP_RX_SLOTS) return;

    CerfRxReclaimSlot(api, mp, i);

    if (CerfRxTryFreePools(api, mp))
        CERF_LOG("ndis: packet pools freed after drain");
}

BOOL CerfNdisPacketRxShutdown(void) {
    const CerfNdisApi* api = CerfNdisResolve();
    CerfMpSlot* mp = CerfMpSelf();
    int i;
    int outstanding = 0;

    mp->pkt_ready   = FALSE;
    mp->pkt_adapter = 0;
    if (mp->pkt_draining == CERF_RX_DRAIN_IDLE)
        InterlockedExchange(&mp->pkt_draining, CERF_RX_DRAIN_PENDING);

    for (i = 0; i < CERF_MP_RX_SLOTS; ++i)
        if (mp->pkt_slot[i].busy) ++outstanding;

    if (CerfRxTryFreePools(api, mp)) return TRUE;
    if (mp->pkt_draining == CERF_RX_DRAIN_IDLE) return TRUE;

    CERF_LOG_X("ndis: unbound, pools not reclaimed here, outstanding",
               (ULONG)outstanding);
    return FALSE;
}
