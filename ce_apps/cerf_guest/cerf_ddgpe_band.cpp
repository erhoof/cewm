#include "cerf_ddgpe.h"

int CerfBrushRowAt(const CerfBltBand& b, int row) {
    return (int)((ULONG)(b.brush_t + b.dt + row) % (ULONG)b.bh);
}

void CerfBrushBandRows(const CerfBltBand& b, int r0, int r1, int* by0, int* by1) {
    const int count = r1 - r0;
    if (count >= b.bh) { *by0 = 0; *by1 = b.bh; return; }
    *by0 = CerfBrushRowAt(b, r0);
    *by1 = *by0 + count;
}

int CerfBrushClampEnd(const CerfBltBand& b, int r0, int r1) {
    if (!b.has_brush || !b.brush_banded) return r1;
    const int allowed = b.bh - CerfBrushRowAt(b, r0);
    if (r1 - r0 > allowed) r1 = r0 + allowed;
    const int wrap = -(b.brush_t + b.dt);
    if (wrap > r0 && wrap < r1) r1 = wrap;
    return r1;
}

int CerfBrushClampStart(const CerfBltBand& b, int r0, int r1) {
    if (!b.has_brush || !b.brush_banded) return r0;
    const int lo = r1 - 1 - CerfBrushRowAt(b, r1 - 1);
    return (r0 < lo) ? lo : r0;
}

bool CerfBandClip(const CerfBltBand& b, GPEBltParms* p, int* cl, int* cr) {
    RECTL bclip;
    CerfEffectiveClip(&bclip, p->prclClip, p->pDst);
    int l = b.dl, r = b.dl + b.width, t = b.dt, bo = b.dt + b.height;
    CerfClampWindow(&l, &t, &r, &bo, &bclip);
    *cl = l;
    *cr = r;
    return r > l && bo > t;
}

ULONG CerfBandSpanBytes(const CerfBltBand& b, int cl, int cr, int r0, int r1) {
    ULONG t = 0;
    if (!b.dst_fb)
        t += CerfSpanBytes(cl, b.dt + r0, cr, b.dt + r1, b.dst_stride, b.dst_bits);
    if (b.has_src && !b.src_fb) {
        const int sy0 = b.use_lut_y ? CerfSrcDyAt(b.height, b.src_h, r0) : r0;
        const int sy1 = b.use_lut_y ? CerfSrcDyAt(b.height, b.src_h, r1 - 1) : (r1 - 1);
        t += CerfSpanBytes(b.sl, b.st + sy0, b.sr, b.st + sy1 + 1,
                           b.src_stride, b.src_bits);
    }
    if (b.has_mask)
        t += CerfSpanBytes(b.ml, b.mt + r0, b.mr, b.mt + r1,
                           b.mask_stride, b.mask_bits);
    if (b.has_brush && b.brush_banded) {
        int by0 = 0, by1 = 0;
        CerfBrushBandRows(b, r0, r1, &by0, &by1);
        t += CerfSpanBytes(0, by0, b.bw, by1, b.brush_stride, b.brush_bits);
    }
    return t;
}

int CerfBandEnd(const CerfBltBand& b, int cl, int cr, ULONG budget, int r0) {
    int r1 = CerfBrushClampEnd(b, r0, b.height);
    while (r1 > r0 + 1) {
        if (CerfBandSpanBytes(b, cl, cr, r0, r1) <= budget) break;
        r1 = r0 + (r1 - r0) / 2;
    }
    return r1;
}

int CerfBandStart(const CerfBltBand& b, int cl, int cr, ULONG budget, int r1) {
    int r0 = CerfBrushClampStart(b, 0, r1);
    while (r0 < r1 - 1) {
        if (CerfBandSpanBytes(b, cl, cr, r0, r1) <= budget) break;
        r0 = r1 - (r1 - r0) / 2;
    }
    return r0;
}

void CerfDDGPE::EmitBltBandsUp(const CerfBltBand& b, GPEBltParms* p,
                               ULONG budget, int cl, int cr) {
    int r1 = b.height;
    for (int i = 0; i < b.height; ++i) {
        if (r1 <= 0) break;
        int r0 = CerfBandStart(b, cl, cr, budget, r1);
        EmitBltBand(b, p, r0, r1);
        r1 = r0;
    }
}

void CerfDDGPE::EmitBltBandsDown(const CerfBltBand& b, GPEBltParms* p,
                                 ULONG budget, int cl, int cr) {
    int r0 = 0;
    for (int i = 0; i < b.height; ++i) {
        if (r0 >= b.height) break;
        int r1 = CerfBandEnd(b, cl, cr, budget, r0);
        EmitBltBand(b, p, r0, r1);
        r0 = r1;
    }
}

void CerfDDGPE::EmitBltBands(const CerfBltBand& b, GPEBltParms* p, ULONG budget,
                             CerfBandOrder order) {
    int cl = 0, cr = 0;
    if (!CerfBandClip(b, p, &cl, &cr)) return;
    if (order == kCerfBandUp) EmitBltBandsUp(b, p, budget, cl, cr);
    else                      EmitBltBandsDown(b, p, budget, cl, cr);
}
