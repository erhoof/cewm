#include "cerf_ndis.h"
#include "cerf_ndis_miniport.h"
#include "cerf_debug_log.h"

#include "../../cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"
#include "../../cerf/peripherals/cerf_virt/cerf_virt_nic_regs.h"

#define OID_GEN_SUPPORTED_LIST          0x00010101
#define OID_GEN_HARDWARE_STATUS         0x00010102
#define OID_GEN_MEDIA_SUPPORTED         0x00010103
#define OID_GEN_MEDIA_IN_USE            0x00010104
#define OID_GEN_MAXIMUM_LOOKAHEAD       0x00010105
#define OID_GEN_MAXIMUM_FRAME_SIZE      0x00010106
#define OID_GEN_LINK_SPEED              0x00010107
#define OID_GEN_TRANSMIT_BUFFER_SPACE   0x00010108
#define OID_GEN_RECEIVE_BUFFER_SPACE    0x00010109
#define OID_GEN_TRANSMIT_BLOCK_SIZE     0x0001010A
#define OID_GEN_RECEIVE_BLOCK_SIZE      0x0001010B
#define OID_GEN_VENDOR_ID               0x0001010C
#define OID_GEN_VENDOR_DESCRIPTION      0x0001010D
#define OID_GEN_CURRENT_PACKET_FILTER   0x0001010E
#define OID_GEN_CURRENT_LOOKAHEAD       0x0001010F
#define OID_GEN_DRIVER_VERSION          0x00010110
#define OID_GEN_MAXIMUM_TOTAL_SIZE      0x00010111
#define OID_GEN_MAC_OPTIONS             0x00010113
#define OID_GEN_MEDIA_CONNECT_STATUS    0x00010114
#define OID_GEN_VENDOR_DRIVER_VERSION   0x00010116
#define OID_802_3_PERMANENT_ADDRESS     0x01010101
#define OID_802_3_CURRENT_ADDRESS       0x01010102
#define OID_802_3_MULTICAST_LIST        0x01010103
#define OID_802_3_MAXIMUM_LIST_SIZE     0x01010104

#define CERF_MP_MULTICAST_MAX           32
#define CERF_MP_MAC_OPTIONS             15
#define CERF_MP_TOTAL_MAX               1514
#define CERF_MP_BLOCK_SIZE              1518
#define CERF_MP_VENDOR_INDEX            1

static NDIS_STATUS CerfMpReply(void* buf, UINT len, const void* src, UINT n,
                               UINT* written, UINT* needed) {
    if (len < n) { *needed = n; *written = 0; return CERF_NDIS_STATUS_INVALID_LENGTH; }
    memcpy(buf, src, n);
    *written = n;
    *needed  = 0;
    return CERF_NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS CerfMpReplyU32(void* buf, UINT len, ULONG v,
                                  UINT* written, UINT* needed) {
    return CerfMpReply(buf, len, &v, 4, written, needed);
}

extern "C" NDIS_STATUS CerfMpQueryInformation(NDIS_HANDLE adapter, ULONG oid,
                                              void* buf, UINT len,
                                              UINT* written, UINT* needed) {
    static const ULONG kSupported[] = {
        OID_GEN_SUPPORTED_LIST, OID_GEN_HARDWARE_STATUS,
        OID_GEN_MEDIA_SUPPORTED, OID_GEN_MEDIA_IN_USE,
        OID_GEN_MAXIMUM_LOOKAHEAD, OID_GEN_MAXIMUM_FRAME_SIZE,
        OID_GEN_LINK_SPEED, OID_GEN_TRANSMIT_BUFFER_SPACE,
        OID_GEN_RECEIVE_BUFFER_SPACE, OID_GEN_TRANSMIT_BLOCK_SIZE,
        OID_GEN_RECEIVE_BLOCK_SIZE, OID_GEN_VENDOR_ID,
        OID_GEN_VENDOR_DESCRIPTION, OID_GEN_CURRENT_PACKET_FILTER,
        OID_GEN_CURRENT_LOOKAHEAD, OID_GEN_DRIVER_VERSION,
        OID_GEN_MAXIMUM_TOTAL_SIZE, OID_GEN_MAC_OPTIONS,
        OID_GEN_MEDIA_CONNECT_STATUS, OID_GEN_VENDOR_DRIVER_VERSION,
        OID_802_3_PERMANENT_ADDRESS, OID_802_3_CURRENT_ADDRESS,
        OID_802_3_MULTICAST_LIST, OID_802_3_MAXIMUM_LIST_SIZE
    };
    static const char kDesc[] = "CERF Guest Additions Ethernet";

    (void)adapter;
    CERF_LOG_X_DEV("ndis: query oid", oid);
    switch (oid) {
    case OID_GEN_SUPPORTED_LIST:
        return CerfMpReply(buf, len, kSupported, sizeof(kSupported), written, needed);
    case OID_GEN_HARDWARE_STATUS:       return CerfMpReplyU32(buf, len, 0, written, needed);
    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        return CerfMpReplyU32(buf, len, CERF_NDIS_MEDIUM_802_3, written, needed);
    case OID_GEN_MAXIMUM_LOOKAHEAD:
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        return CerfMpReplyU32(buf, len, CERF_MP_FRAME_MAX, written, needed);
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        return CerfMpReplyU32(buf, len, CERF_MP_BLOCK_SIZE, written, needed);
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
        return CerfMpReplyU32(buf, len, CERF_MP_TOTAL_MAX, written, needed);
    case OID_GEN_LINK_SPEED:            return CerfMpReplyU32(buf, len, 100000, written, needed);
    case OID_GEN_TRANSMIT_BUFFER_SPACE:
        return CerfMpReplyU32(buf, len,
                              CerfVirt::kNicTxSlots * CERF_MP_BLOCK_SIZE, written, needed);
    case OID_GEN_RECEIVE_BUFFER_SPACE:
        return CerfMpReplyU32(buf, len,
                              CerfVirt::kNicRxSlots * CERF_MP_BLOCK_SIZE, written, needed);
    case OID_GEN_VENDOR_ID: {
        const UCHAR* m = CerfMpMac();
        return CerfMpReplyU32(buf, len,
                              ((ULONG)m[2] << 16) | ((ULONG)m[1] << 8) |
                                  CERF_MP_VENDOR_INDEX,
                              written, needed);
    }
    case OID_GEN_VENDOR_DESCRIPTION:
        return CerfMpReply(buf, len, kDesc, sizeof(kDesc), written, needed);
    case OID_GEN_CURRENT_PACKET_FILTER:
        return CerfMpReplyU32(buf, len, CerfMpFilterGet(), written, needed);
    case OID_GEN_CURRENT_LOOKAHEAD:
        return CerfMpReplyU32(buf, len, CerfMpLookaheadGet(), written, needed);
    case OID_GEN_DRIVER_VERSION:
    case OID_GEN_VENDOR_DRIVER_VERSION: {
        USHORT ver = 0x0400;
        return CerfMpReply(buf, len, &ver, 2, written, needed);
    }
    case OID_GEN_MAC_OPTIONS:
        return CerfMpReplyU32(buf, len, CERF_MP_MAC_OPTIONS, written, needed);
    case OID_GEN_MEDIA_CONNECT_STATUS:
        return CerfMpReplyU32(buf, len, CerfMpLinkUp() ? 0 : 1, written, needed);
    case OID_802_3_PERMANENT_ADDRESS:
    case OID_802_3_CURRENT_ADDRESS:
        return CerfMpReply(buf, len, CerfMpMac(), 6, written, needed);
    case OID_802_3_MULTICAST_LIST:      *written = 0; *needed = 0; return CERF_NDIS_STATUS_SUCCESS;
    case OID_802_3_MAXIMUM_LIST_SIZE:
        return CerfMpReplyU32(buf, len, CERF_MP_MULTICAST_MAX, written, needed);
    default:
        *written = 0; *needed = 0;
        return CERF_NDIS_STATUS_NOT_SUPPORTED;
    }
}

extern "C" NDIS_STATUS CerfMpSetInformation(NDIS_HANDLE adapter, ULONG oid,
                                            void* buf, UINT len,
                                            UINT* read, UINT* needed) {
    ULONG v = 0;

    (void)adapter;
    *read = 0; *needed = 0;
    CERF_LOG_X_DEV("ndis: set oid", oid);
    switch (oid) {
    case OID_GEN_CURRENT_PACKET_FILTER:
        if (len < 4) { *needed = 4; return CERF_NDIS_STATUS_INVALID_LENGTH; }
        memcpy(&v, buf, 4);
        CerfMpFilterSet(v);
        *read = 4;
        CERF_LOG_X_DEV("ndis: packet filter now", v);
        return CERF_NDIS_STATUS_SUCCESS;
    case OID_GEN_CURRENT_LOOKAHEAD:
        if (len < 4) { *needed = 4; return CERF_NDIS_STATUS_INVALID_LENGTH; }
        memcpy(&v, buf, 4);
        CerfMpLookaheadSet(v);
        *read = 4;
        CERF_LOG_X_DEV("ndis: lookahead now", v);
        return CERF_NDIS_STATUS_SUCCESS;
    case OID_802_3_MULTICAST_LIST:
        if (len > CERF_MP_MULTICAST_MAX * 6) return CERF_NDIS_STATUS_FAILURE;
        *read = len;
        return CERF_NDIS_STATUS_SUCCESS;
    default:
        return CERF_NDIS_STATUS_NOT_SUPPORTED;
    }
}
