#pragma once

#include <windows.h>
#include <winddi.h>

#include "cerf_gpe.h"

struct CerfTmpSurf {
    GPESurf* surf;
    GPESurf  local;
    BOOL     owned;
};

static inline GPESurf* CerfWrapSurfobj(CerfTmpSurf* tmp, SURFOBJ* pso) {
    tmp->surf  = NULL;
    tmp->owned = FALSE;
    if (!pso) return NULL;
    if (pso->dhsurf) {
        tmp->surf = (GPESurf*)pso->dhsurf;
        return tmp->surf;
    }
    if (pso->iBitmapFormat > 9) return NULL;
    tmp->local.Init((int)pso->sizlBitmap.cx, (int)pso->sizlBitmap.cy,
                    pso->pvScan0, (int)pso->lDelta,
                    CerfIFormatToEGPE(pso->iBitmapFormat));
    tmp->surf  = &tmp->local;
    tmp->owned = TRUE;
    return tmp->surf;
}
