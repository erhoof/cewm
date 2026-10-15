
#pragma once

#include <windows.h>

#pragma pack(push, 4)

#define WM_DD_ROP_SPACE (256 / 32)

typedef struct _Wm_DDSCAPS { DWORD dwCaps; } Wm_DDSCAPS;

typedef struct _Wm_DDCAPS {
    DWORD       dwSize;
    DWORD       dwVidMemTotal;
    DWORD       dwVidMemFree;
    DWORD       dwVidMemStride;
    Wm_DDSCAPS  ddsCaps;
    DWORD       dwNumFourCCCodes;
    DWORD       dwPalCaps;
    DWORD       dwBltCaps;
    DWORD       dwCKeyCaps;
    DWORD       dwAlphaCaps;
    DWORD       dwRops[WM_DD_ROP_SPACE];
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
    DWORD       dwSize;
    DWORD       dwFlags;
    PVOID       lpDDCallbacks;
    PVOID       lpDDSurfaceCallbacks;
    PVOID       lpDDPaletteCallbacks;
    PVOID       GetDriverInfo;
    Wm_DDCAPS   ddCaps;
    Wm_DDCAPS   ddHelCaps;
    PVOID       lpdwFourCC;
} Wm_DDHALINFO;

#pragma pack(pop)
