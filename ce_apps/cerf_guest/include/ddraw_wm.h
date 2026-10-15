#pragma once

#include <windows.h>
#include "ddraw_common.h"

#pragma pack(push, 4)

typedef struct _Wm_DDCAPS {
    DWORD       dwSize;
    DWORD       dwVidMemTotal;
    DWORD       dwVidMemFree;
    DWORD       dwVidMemStride;
    CerfDDSCAPS ddsCaps;
    DWORD       dwNumFourCCCodes;
    DWORD       dwPalCaps;
    DWORD       dwBltCaps;
    DWORD       dwCKeyCaps;
    DWORD       dwAlphaCaps;
    DWORD       dwRops[CERF_DD_ROP_SPACE];
    DWORD       dwOverlayCaps;
    DWORD       dwMaxVisibleOverlays;
    DWORD       dwCurrVisibleOverlays;
    DWORD       dwAlignBoundarySrc;
    DWORD       dwAlignSizeSrc;
    DWORD       dwAlignBoundaryDest;
    DWORD       dwAlignSizeDest;
    DWORD       dwMinOverlayStretch;
    DWORD       dwMaxOverlayStretch;
    DWORD       dwMiscCaps;
} Wm_DDCAPS;

typedef struct _Wm_DDHALINFO {
    DWORD     dwSize;
    DWORD     dwFlags;
    PVOID     lpDDCallbacks;
    PVOID     lpDDSurfaceCallbacks;
    PVOID     lpDDPaletteCallbacks;
    PVOID     GetDriverInfo;
    Wm_DDCAPS ddCaps;
    Wm_DDCAPS ddHelCaps;
    PVOID     lpdwFourCC;
} Wm_DDHALINFO;

#pragma pack(pop)

typedef char Wm_DDCAPS_size_check[(sizeof(Wm_DDCAPS) == 112) ? 1 : -1];
typedef char Wm_DDHALINFO_size_check[(sizeof(Wm_DDHALINFO) == 252) ? 1 : -1];
