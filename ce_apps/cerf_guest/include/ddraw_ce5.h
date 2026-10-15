#pragma once

#include <windows.h>
#include <stddef.h>
#include "ddraw_common.h"

#pragma pack(push, 4)

#define CE5_DDSCAPS_ALPHA          0x00000002
#define CE5_DDSCAPS_BACKBUFFER     0x00000004
#define CE5_DDSCAPS_COMPLEX        0x00000008
#define CE5_DDSCAPS_FLIP           0x00000010
#define CE5_DDSCAPS_FRONTBUFFER    0x00000020
#define CE5_DDSCAPS_OVERLAY        0x00000080
#define CE5_DDSCAPS_PALETTE        0x00000100
#define CE5_DDSCAPS_PRIMARYSURFACE 0x00000200
#define CE5_DDSCAPS_SYSTEMMEMORY   0x00000800
#define CE5_DDSCAPS_VIDEOMEMORY    0x00004000
#define CE5_DDSCAPS_OWNDC          0x00040000

#define CE5_DDSD_CAPS        0x00000001
#define CE5_DDSD_HEIGHT      0x00000002
#define CE5_DDSD_WIDTH       0x00000004
#define CE5_DDSD_PITCH       0x00000008
#define CE5_DDSD_LPSURFACE   0x00000800
#define CE5_DDSD_PIXELFORMAT 0x00001000
#define CE5_DDSD_LINEARSIZE  0x00080000

#define CE5_DDCAPS_BLT           0x00000040
#define CE5_DDCAPS_CANBLTSYSMEM  0x80000000

typedef struct _Ce5_DDSURFACEDESC {
    DWORD             dwSize;
    DWORD             dwFlags;
    DWORD             dwHeight;
    DWORD             dwWidth;
    union { LONG lPitch; DWORD dwLinearSize; } u1;
    DWORD             dwBackBufferCount;
    union { DWORD dwMipMapCount; DWORD dwZBufferBitDepth; DWORD dwRefreshRate; } u2;
    DWORD             dwAlphaBitDepth;
    DWORD             dwReserved;
    LPVOID            lpSurface;
    CerfDDCOLORKEY    ddckCKDestOverlay;
    CerfDDCOLORKEY    ddckCKDestBlt;
    CerfDDCOLORKEY    ddckCKSrcOverlay;
    CerfDDCOLORKEY    ddckCKSrcBlt;
    CerfDDPIXELFORMAT ddpfPixelFormat;
    CerfDDSCAPS       ddsCaps;
} Ce5_DDSURFACEDESC;

typedef struct _Ce5_VIDMEMINFO {
    DWORD             fpPrimary;
    DWORD             dwFlags;
    DWORD             dwDisplayWidth;
    DWORD             dwDisplayHeight;
    LONG              lDisplayPitch;
    CerfDDPIXELFORMAT ddpfDisplay;
    DWORD             dwOffscreenAlign;
    DWORD             dwOverlayAlign;
    DWORD             dwTextureAlign;
    DWORD             dwZBufferAlign;
    DWORD             dwAlphaAlign;
    DWORD             dwNumHeaps;
    DWORD             pvmList;
} Ce5_VIDMEMINFO;

typedef struct _Ce5_DDCORECAPS {
    DWORD       dwSize;
    DWORD       dwCaps;
    DWORD       dwCaps2;
    DWORD       dwCKeyCaps;
    DWORD       dwFXCaps;
    DWORD       dwFXAlphaCaps;
    DWORD       dwPalCaps;
    DWORD       dwSVCaps;
    DWORD       dwAlphaBltConstBitDepths;
    DWORD       dwAlphaBltPixelBitDepths;
    DWORD       dwAlphaBltSurfaceBitDepths;
    DWORD       dwAlphaOverlayConstBitDepths;
    DWORD       dwAlphaOverlayPixelBitDepths;
    DWORD       dwAlphaOverlaySurfaceBitDepths;
    DWORD       dwZBufferBitDepths;
    DWORD       dwVidMemTotal;
    DWORD       dwVidMemFree;
    DWORD       dwMaxVisibleOverlays;
    DWORD       dwCurrVisibleOverlays;
    DWORD       dwNumFourCCCodes;
    DWORD       dwAlignBoundarySrc;
    DWORD       dwAlignSizeSrc;
    DWORD       dwAlignBoundaryDest;
    DWORD       dwAlignSizeDest;
    DWORD       dwAlignStrideAlign;
    DWORD       dwRops[CERF_DD_ROP_SPACE];
    CerfDDSCAPS ddsCaps;
    DWORD       dwMinOverlayStretch;
    DWORD       dwMaxOverlayStretch;
    DWORD       dwMinLiveVideoStretch;
    DWORD       dwMaxLiveVideoStretch;
    DWORD       dwMinHwCodecStretch;
    DWORD       dwMaxHwCodecStretch;
    DWORD       dwReserved1;
    DWORD       dwReserved2;
    DWORD       dwReserved3;
    DWORD       dwSVBCaps;
    DWORD       dwSVBCKeyCaps;
    DWORD       dwSVBFXCaps;
    DWORD       dwSVBRops[CERF_DD_ROP_SPACE];
    DWORD       dwVSBCaps;
    DWORD       dwVSBCKeyCaps;
    DWORD       dwVSBFXCaps;
    DWORD       dwVSBRops[CERF_DD_ROP_SPACE];
    DWORD       dwSSBCaps;
    DWORD       dwSSBCKeyCaps;
    DWORD       dwSSBFXCaps;
    DWORD       dwSSBRops[CERF_DD_ROP_SPACE];
    DWORD       dwMaxVideoPorts;
    DWORD       dwCurrVideoPorts;
    DWORD       dwSVBCaps2;
} Ce5_DDCORECAPS;

typedef struct _Ce5_DDHALMODEINFO {
    DWORD dwWidth;
    DWORD dwHeight;
    LONG  lPitch;
    DWORD dwBPP;
    WORD  wFlags;
    WORD  wRefreshRate;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwAlphaBitMask;
} Ce5_DDHALMODEINFO;

typedef struct _Ce5_DDHALINFO {
    DWORD          dwSize;
    PVOID          lpDDCallbacks;
    PVOID          lpDDSurfaceCallbacks;
    PVOID          lpDDPaletteCallbacks;
    Ce5_VIDMEMINFO vmiData;
    Ce5_DDCORECAPS ddCaps;
    DWORD          dwMonitorFrequency;
    PVOID          GetDriverInfo;
    DWORD          dwModeIndex;
    PVOID          lpdwFourCC;
    DWORD          dwNumModes;
    PVOID          lpModeInfo;
    DWORD          dwFlags;
    PVOID          lpPDevice;
    DWORD          hInstance;
    ULONG_PTR      lpD3DGlobalDriverData;
    ULONG_PTR      lpD3DHALCallbacks;
    PVOID          lpDDExeBufCallbacks;
} Ce5_DDHALINFO;

#define CE5_DDHAL_CB32_CREATESURFACE        0x00000002
#define CE5_DDHAL_CB32_WAITFORVERTICALBLANK 0x00000010
#define CE5_DDHAL_CB32_CANCREATESURFACE     0x00000020
#define CE5_DDHAL_CB32_CREATEPALETTE        0x00000040

typedef struct _Ce5_DDHAL_DDCALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    PVOID DestroyDriver;
    PVOID CreateSurface;
    PVOID SetColorKey;
    PVOID SetMode;
    PVOID WaitForVerticalBlank;
    PVOID CanCreateSurface;
    PVOID CreatePalette;
    PVOID GetScanLine;
    PVOID SetExclusiveMode;
    PVOID FlipToGDISurface;
} Ce5_DDHAL_DDCALLBACKS;

#define CE5_DDHAL_SURFCB32_DESTROYSURFACE 0x00000001
#define CE5_DDHAL_SURFCB32_FLIP           0x00000002
#define CE5_DDHAL_SURFCB32_LOCK           0x00000008
#define CE5_DDHAL_SURFCB32_UNLOCK         0x00000010
#define CE5_DDHAL_SURFCB32_BLT            0x00000020
#define CE5_DDHAL_SURFCB32_SETCOLORKEY    0x00000040
#define CE5_DDHAL_SURFCB32_GETBLTSTATUS   0x00000100
#define CE5_DDHAL_SURFCB32_GETFLIPSTATUS  0x00000200
#define CE5_DDHAL_SURFCB32_SETPALETTE     0x00002000

typedef struct _Ce5_DDHAL_DDSURFACECALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    PVOID DestroySurface;
    PVOID Flip;
    PVOID SetClipList;
    PVOID Lock;
    PVOID Unlock;
    PVOID Blt;
    PVOID SetColorKey;
    PVOID AddAttachedSurface;
    PVOID GetBltStatus;
    PVOID GetFlipStatus;
    PVOID UpdateOverlay;
    PVOID SetOverlayPosition;
    PVOID reserved4;
    PVOID SetPalette;
    PVOID reserved5;
    PVOID reserved6;
} Ce5_DDHAL_DDSURFACECALLBACKS;

typedef struct _Ce5_DDRAWSURFACE_GBL {
    DWORD  dwRefCnt;
    DWORD  dwGlobalFlags;
    PVOID  lpRectList;
    PVOID  lpVidMemHeap;
    PVOID  lpDD;
    DWORD  fpVidMem;
    union { LONG lPitch; DWORD dwLinearSize; } u1;
    WORD   wHeight;
    WORD   wWidth;
    DWORD  dwUsageCount;
    ULONG_PTR dwReserved1;
} Ce5_DDRAWSURFACE_GBL;

typedef struct _Ce5_DDRAWSURFACE_LCL {
    PVOID                 lpSurfMore;
    Ce5_DDRAWSURFACE_GBL* lpGbl;
    ULONG_PTR             hDDSurface;
    PVOID                 lpAttachList;
    PVOID                 lpAttachListFrom;
    DWORD                 dwLocalRefCnt;
    DWORD                 dwProcessId;
    DWORD                 dwFlags;
    CerfDDSCAPS           ddsCaps;
    PVOID                 lpDDPalette;
    PVOID                 lpDDClipper;
    DWORD                 dwModeCreatedIn;
    DWORD                 dwBackBufferCount;
    CerfDDCOLORKEY        ddckCKDestBlt;
    CerfDDCOLORKEY        ddckCKSrcBlt;
    ULONG_PTR             hDC;
    ULONG_PTR             dwReserved1;
} Ce5_DDRAWSURFACE_LCL;

typedef struct _Ce5_DDHAL_CREATESURFACEDATA {
    PVOID   lpDD;
    PVOID   lpDDSurfaceDesc;
    PVOID*  lplpSList;
    DWORD   dwSCnt;
    HRESULT ddRVal;
    PVOID   CreateSurface;
} Ce5_DDHAL_CREATESURFACEDATA;

typedef struct _Ce5_DDHAL_CANCREATESURFACEDATA {
    PVOID   lpDD;
    PVOID   lpDDSurfaceDesc;
    DWORD   bIsDifferentPixelFormat;
    HRESULT ddRVal;
} Ce5_DDHAL_CANCREATESURFACEDATA;

typedef struct _Ce5_DDHAL_LOCKDATA {
    PVOID   lpDD;
    PVOID   lpDDSurface;
    DWORD   bHasRect;
    RECTL   rArea;
    PVOID   lpSurfData;
    HRESULT ddRVal;
    PVOID   Lock;
    DWORD   dwFlags;
} Ce5_DDHAL_LOCKDATA;

typedef struct _Ce5_DDHAL_UNLOCKDATA {
    PVOID   lpDD;
    PVOID   lpDDSurface;
    HRESULT ddRVal;
} Ce5_DDHAL_UNLOCKDATA;

typedef struct _Ce5_DDHAL_DESTROYSURFACEDATA {
    PVOID   lpDD;
    PVOID   lpDDSurface;
    HRESULT ddRVal;
} Ce5_DDHAL_DESTROYSURFACEDATA;

typedef struct _Ce5_DDHAL_FLIPDATA {
    PVOID   lpDD;
    PVOID   lpSurfCurr;
    PVOID   lpSurfTarg;
    DWORD   dwFlags;
    HRESULT ddRVal;
} Ce5_DDHAL_FLIPDATA;

typedef struct _Ce5_DDHAL_SETCOLORKEYDATA {
    PVOID          lpDD;
    PVOID          lpDDSurface;
    DWORD          dwFlags;
    CerfDDCOLORKEY ckNew;
    HRESULT        ddRVal;
} Ce5_DDHAL_SETCOLORKEYDATA;

typedef struct _Ce5_DDHAL_GETBLTSTATUSDATA {
    PVOID   lpDD;
    PVOID   lpDDSurface;
    DWORD   dwFlags;
    HRESULT ddRVal;
} Ce5_DDHAL_GETBLTSTATUSDATA;

typedef struct _Ce5_DDHAL_GETFLIPSTATUSDATA {
    PVOID   lpDD;
    PVOID   lpDDSurface;
    DWORD   dwFlags;
    HRESULT ddRVal;
} Ce5_DDHAL_GETFLIPSTATUSDATA;

typedef struct _Ce5_DDHAL_SETPALETTEDATA {
    PVOID   lpDD;
    PVOID   lpDDSurface;
    PVOID   lpDDPalette;
    BOOL    Attach;
    HRESULT ddRVal;
} Ce5_DDHAL_SETPALETTEDATA;

typedef struct _Ce5_DDHAL_CREATEPALETTEDATA {
    PVOID          lpDD;
    LPPALETTEENTRY lpColorTable;
    PVOID          lpDDPalette;
    HRESULT        ddRVal;
} Ce5_DDHAL_CREATEPALETTEDATA;

typedef struct _Ce5_DDHAL_WAITFORVERTICALBLANKDATA {
    PVOID     lpDD;
    DWORD     dwFlags;
    DWORD     bIsInVB;
    ULONG_PTR hEvent;
    HRESULT   ddRVal;
} Ce5_DDHAL_WAITFORVERTICALBLANKDATA;

typedef struct _Ce5_DDHAL_BLTDATA {
    PVOID       lpDD;
    PVOID       lpDDDestSurface;
    RECTL       rDest;
    PVOID       lpDDSrcSurface;
    RECTL       rSrc;
    DWORD       dwFlags;
    DWORD       dwROPFlags;
    CerfDDBLTFX bltFX;
    HRESULT     ddRVal;
} Ce5_DDHAL_BLTDATA;

#pragma pack(pop)

typedef char Ce5_DDHALINFO_size_check[(sizeof(Ce5_DDHALINFO) == 460) ? 1 : -1];
typedef char Ce5_SurfCb_size_check[(sizeof(Ce5_DDHAL_DDSURFACECALLBACKS) == 72) ? 1 : -1];

typedef char Ce5_Lcl_gbl_check[(offsetof(Ce5_DDRAWSURFACE_LCL, lpGbl) == 0x04) ? 1 : -1];
typedef char Ce5_Lcl_caps_check[(offsetof(Ce5_DDRAWSURFACE_LCL, ddsCaps) == 0x20) ? 1 : -1];
typedef char Ce5_Lcl_resv_check[(offsetof(Ce5_DDRAWSURFACE_LCL, dwReserved1) == 0x48) ? 1 : -1];
typedef char Ce5_Gbl_vidmem_check[(offsetof(Ce5_DDRAWSURFACE_GBL, fpVidMem) == 0x14) ? 1 : -1];
