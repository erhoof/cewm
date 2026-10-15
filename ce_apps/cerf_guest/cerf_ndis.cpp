#include "cerf_ndis.h"
#include "cerf_debug_log.h"

typedef struct {
    int         resolved;
    CerfNdisApi api;
} CerfNdisSlot;

static CerfNdisSlot s_ndis_slot;

static FARPROC CerfNdisSym(HMODULE h, const wchar_t* name, const char* aname) {
    FARPROC p = GetProcAddressW(h, name);
    if (!p) CERF_LOG(aname);
    return p;
}
#define CERF_NDIS_SYM(h, n) CerfNdisSym((h), L##n, "ndis: missing export " n)

const CerfNdisApi* CerfNdisResolve(void) {
    CerfNdisSlot* slot = &s_ndis_slot;
    HMODULE h;
    CerfNdisApi* a;

    if (slot->resolved) return slot->api.hndis ? &slot->api : NULL;

    h = LoadLibraryW(L"ndis.dll");
    if (!h) {
        CERF_LOG("ndis: ndis.dll not present on this ROM - miniport disabled");
        return NULL;
    }

    a = &slot->api;
    a->hndis = h;

    a->InitializeWrapper = (PFN_NdisInitializeWrapper)
        CERF_NDIS_SYM(h, "NdisInitializeWrapper");
    a->TerminateWrapper = (PFN_NdisTerminateWrapper)
        CERF_NDIS_SYM(h, "NdisTerminateWrapper");
    a->MRegisterMiniport = (PFN_NdisMRegisterMiniport)
        CERF_NDIS_SYM(h, "NdisMRegisterMiniport");
    a->MSetAttributesEx = (PFN_NdisMSetAttributesEx)
        CERF_NDIS_SYM(h, "NdisMSetAttributesEx");
    a->MIndicateStatus = (PFN_NdisMIndicateStatus)
        CERF_NDIS_SYM(h, "NdisMIndicateStatus");
    a->MIndicateStatusComplete = (PFN_NdisMIndicateStatusComplete)
        CERF_NDIS_SYM(h, "NdisMIndicateStatusComplete");
    a->EthIndicateReceive = (PFN_EthFilterDprIndicateReceive)
        CERF_NDIS_SYM(h, "EthFilterDprIndicateReceive");
    a->EthIndicateReceiveComplete = (PFN_EthFilterDprIndicateReceiveComplete)
        CERF_NDIS_SYM(h, "EthFilterDprIndicateReceiveComplete");
    a->QueryBuffer = (PFN_NdisQueryBuffer)
        CERF_NDIS_SYM(h, "NdisQueryBuffer");

    a->RegisterAdapter = (PFN_NdisRegisterAdapter)
        GetProcAddressW(h, L"NdisRegisterAdapter");
    a->NdsInit = (PFN_NDS_Init)GetProcAddressW(h, L"NDS_Init");

    a->AllocatePacketPool = (PFN_NdisAllocatePacketPool)
        GetProcAddressW(h, L"NdisAllocatePacketPool");
    a->AllocatePacket = (PFN_NdisAllocatePacket)
        GetProcAddressW(h, L"NdisAllocatePacket");
    a->AllocateBufferPool = (PFN_NdisAllocateBufferPool)
        GetProcAddressW(h, L"NdisAllocateBufferPool");
    a->AllocateBuffer = (PFN_NdisAllocateBuffer)
        GetProcAddressW(h, L"NdisAllocateBuffer");
    a->FreeBuffer = (PFN_NdisFreeBuffer)GetProcAddressW(h, L"NdisFreeBuffer");
    a->FreePacket = (PFN_NdisFreePacket)GetProcAddressW(h, L"NdisFreePacket");
    a->FreePacketPool = (PFN_NdisFreePool)
        GetProcAddressW(h, L"NdisFreePacketPool");
    a->FreeBufferPool = (PFN_NdisFreePool)
        GetProcAddressW(h, L"NdisFreeBufferPool");
    a->AcquireSpinLock = (PFN_NdisSpinLock)
        GetProcAddressW(h, L"NdisAcquireSpinLock");
    a->ReleaseSpinLock = (PFN_NdisSpinLock)
        GetProcAddressW(h, L"NdisReleaseSpinLock");
    a->QueryPacket = (PFN_NdisQueryPacket)GetProcAddressW(h, L"NdisQueryPacket");

    if (!a->InitializeWrapper || !a->MRegisterMiniport || !a->MSetAttributesEx ||
        !a->EthIndicateReceive || !a->EthIndicateReceiveComplete ||
        !a->QueryBuffer) {
        CERF_LOG("ndis: required exports absent - miniport disabled");
        a->hndis = NULL;
        FreeLibrary(h);
        slot->resolved = 1;
        return NULL;
    }

    slot->resolved = 1;
    CERF_LOG_X("ndis: resolved, RegisterAdapter present",
               a->RegisterAdapter ? 1 : 0);
    return a;
}
