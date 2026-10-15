#include "cerf_ddgpe.h"

CerfAliasKind CerfDDGPE::BltAliasKind(GPEBltParms* p) {
    if (!p->pSrc || !p->pDst || !p->prclSrc || !p->prclDst) return kCerfAliasNone;
    bool same;
    if (p->pSrc == p->pDst) {
        same = true;
    } else {
        void* buf = p->pSrc->Buffer();
        same = buf != NULL && buf == p->pDst->Buffer();
    }
    if (!same) return kCerfAliasNone;

    if (p->pSrc->Stride() != p->pDst->Stride() ||
        CerfFormatBpp(p->pSrc->Format()) != CerfFormatBpp(p->pDst->Format()))
        return kCerfAliasOpaque;

    const bool y_disjoint = p->prclSrc->bottom <= p->prclDst->top ||
                            p->prclDst->bottom <= p->prclSrc->top;
    const bool x_disjoint = p->prclSrc->right  <= p->prclDst->left ||
                            p->prclDst->right  <= p->prclSrc->left;
    return (y_disjoint || x_disjoint) ? kCerfAliasNone : kCerfAliasGrid;
}

BOOL CerfDDGPE::SnapshotSource(GPEBltParms* p, GPESurf** ppTemp) {
    *ppTemp = NULL;
    const int sw = p->prclSrc->right  - p->prclSrc->left;
    const int sh = p->prclSrc->bottom - p->prclSrc->top;
    if (sw <= 0 || sh <= 0) {
        CERF_LOG_X("cerf_guest: snapshot degenerate source w", (ULONG)sw);
        CERF_LOG_X("cerf_guest: snapshot degenerate source h", (ULONG)sh);
        return FALSE;
    }

    GPESurf* tmp = NULL;
    if (FAILED(AllocSurface(&tmp, sw, sh, p->pSrc->Format(), 0)) || tmp == NULL) {
        CERF_LOG_X("cerf_guest: snapshot alloc failed w", (ULONG)sw);
        CERF_LOG_X("cerf_guest: snapshot alloc failed h", (ULONG)sh);
        return FALSE;
    }

    GPEFormat* sfmt = p->pSrc->FormatPtr();
    GPEFormat* tfmt = tmp->FormatPtr();
    if (sfmt && tfmt && sfmt->m_pPalette && !tfmt->m_pPalette) {
        tfmt->m_pPalette       = sfmt->m_pPalette;
        tfmt->m_PaletteEntries = sfmt->m_PaletteEntries;
        tfmt->m_OwnsPalette    = FALSE;
    }

    RECTL to;
    to.left = 0; to.top = 0; to.right = sw; to.bottom = sh;
    if (FAILED(BltExpanded(tmp, p->pSrc, NULL, (RECT*)&to, (RECT*)p->prclSrc,
                           0u, 0u, 0xCCCCu))) {
        delete tmp;
        CERF_LOG("cerf_guest: snapshot copy failed");
        return FALSE;
    }

    p->srcSnapshot = to;
    p->pSrc        = tmp;
    p->prclSrc     = &p->srcSnapshot;
    p->xPositive   = 1;
    p->yPositive   = 1;
    *ppTemp        = tmp;
    return TRUE;
}

SCODE CerfDDGPE::PlanAliasedBlt(GPEBltParms* p, CerfBandOrder* order,
                                GPESurf** ppTemp) {
    *order  = kCerfBandDown;
    *ppTemp = NULL;
    const CerfAliasKind kind = BltAliasKind(p);
    if (kind == kCerfAliasNone) return S_OK;

    if (kind == kCerfAliasGrid) {
        const int dw = p->prclDst->right  - p->prclDst->left;
        const int dh = p->prclDst->bottom - p->prclDst->top;
        const int sw = p->prclSrc->right  - p->prclSrc->left;
        const int sh = p->prclSrc->bottom - p->prclSrc->top;
        if (((p->bltFlags & 0x0008u) == 0u) || (sw == dw && sh == dh)) {
            *order = p->yPositive ? kCerfBandDown : kCerfBandUp;
            return S_OK;
        }
    }

    return SnapshotSource(p, ppTemp) ? S_OK : E_OUTOFMEMORY;
}
