#pragma once

#include <windows.h>
#include "ddraw_common.h"

#pragma pack(push, 4)

#define CE6_DDSCAPS_ALPHA          0x00000001
#define CE6_DDSCAPS_BACKBUFFER     0x00000002
#define CE6_DDSCAPS_FLIP           0x00000004
#define CE6_DDSCAPS_FRONTBUFFER    0x00000008
#define CE6_DDSCAPS_OVERLAY        0x00000010
#define CE6_DDSCAPS_PALETTE        0x00000020
#define CE6_DDSCAPS_PRIMARYSURFACE 0x00000040
#define CE6_DDSCAPS_SYSTEMMEMORY   0x00000080
#define CE6_DDSCAPS_VIDEOMEMORY    0x00000100
#define CE6_DDSCAPS_OWNDC          0x00008000

#define CE6_DDSD_CAPS        0x00000001
#define CE6_DDSD_HEIGHT      0x00000002
#define CE6_DDSD_WIDTH       0x00000004
#define CE6_DDSD_PITCH       0x00000008
#define CE6_DDSD_XPITCH      0x00000010
#define CE6_DDSD_LPSURFACE   0x00000800
#define CE6_DDSD_PIXELFORMAT 0x00001000
#define CE6_DDSD_SURFACESIZE 0x00080000

typedef struct _Ce6_DDSURFACEDESC {
    DWORD             dwSize;
    DWORD             dwFlags;
    DWORD             dwHeight;
    DWORD             dwWidth;
    LONG              lPitch;
    LONG              lXPitch;
    DWORD             dwBackBufferCount;
    DWORD             dwRefreshRate;
    LPVOID            lpSurface;
    CerfDDCOLORKEY    ddckCKDestOverlay;
    CerfDDCOLORKEY    ddckCKDestBlt;
    CerfDDCOLORKEY    ddckCKSrcOverlay;
    CerfDDCOLORKEY    ddckCKSrcBlt;
    CerfDDPIXELFORMAT ddpfPixelFormat;
    CerfDDSCAPS       ddsCaps;
    DWORD             dwSurfaceSize;
} Ce6_DDSURFACEDESC;

typedef struct _Ce6_DDCAPS {
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
    DWORD       dwMinVideoStretch;
    DWORD       dwMaxVideoStretch;
    DWORD       dwMaxVideoPorts;
    DWORD       dwCurrVideoPorts;
} Ce6_DDCAPS;

typedef struct _Ce6_DDRAWSURFACE_LCL {
    CerfDDrawGbl pDD;
    DWORD        ProcessOwner;
    CerfDDSCAPS  ddsCaps;
    void*        Bitmap;
    DWORD        dwReserved1;
} Ce6_DDRAWSURFACE_LCL;

typedef struct _Ce6_DDHAL_CREATESURFACEDATA {
    CerfDDrawGbl            lpDD;
    Ce6_DDSURFACEDESC*      lpDDSurfaceDesc;
    DWORD                   dwSCnt;
    Ce6_DDRAWSURFACE_LCL**  lplpSList;
    HRESULT                 ddRVal;
} Ce6_DDHAL_CREATESURFACEDATA;

typedef struct _Ce6_DDHAL_CANCREATESURFACEDATA {
    CerfDDrawGbl       lpDD;
    Ce6_DDSURFACEDESC* lpDDSurfaceDesc;
    DWORD              bIsDifferentPixelFormat;
    HRESULT            ddRVal;
} Ce6_DDHAL_CANCREATESURFACEDATA;

typedef struct _Ce6_DDHAL_DESTROYSURFACEDATA {
    CerfDDrawGbl          lpDD;
    Ce6_DDRAWSURFACE_LCL* lpDDSurface;
    HRESULT               ddRVal;
} Ce6_DDHAL_DESTROYSURFACEDATA;

typedef struct _Ce6_DDHAL_FLIPDATA {
    CerfDDrawGbl          lpDD;
    Ce6_DDRAWSURFACE_LCL* lpSurfCurr;
    Ce6_DDRAWSURFACE_LCL* lpSurfTarg;
    DWORD                 dwFlags;
    HRESULT               ddRVal;
} Ce6_DDHAL_FLIPDATA;

typedef struct _Ce6_DDHAL_LOCKDATA {
    CerfDDrawGbl          lpDD;
    Ce6_DDRAWSURFACE_LCL* lpDDSurface;
    BOOL                  bHasRect;
    RECT                  rArea;
    DWORD                 dwFlags;
    LPVOID                lpSurfData;
    HRESULT               ddRVal;
} Ce6_DDHAL_LOCKDATA;

typedef struct _Ce6_DDHAL_UNLOCKDATA {
    CerfDDrawGbl          lpDD;
    Ce6_DDRAWSURFACE_LCL* lpDDSurface;
    HRESULT               ddRVal;
} Ce6_DDHAL_UNLOCKDATA;

typedef struct _Ce6_DDHAL_SETCOLORKEYDATA {
    CerfDDrawGbl          lpDD;
    Ce6_DDRAWSURFACE_LCL* lpDDSurface;
    DWORD                 dwFlags;
    CerfDDCOLORKEY        ckNew;
    HRESULT               ddRVal;
} Ce6_DDHAL_SETCOLORKEYDATA;

typedef struct _Ce6_DDHAL_GETBLTSTATUSDATA {
    CerfDDrawGbl          lpDD;
    Ce6_DDRAWSURFACE_LCL* lpDDSurface;
    DWORD                 dwFlags;
    HRESULT               ddRVal;
} Ce6_DDHAL_GETBLTSTATUSDATA;

typedef struct _Ce6_DDHAL_GETFLIPSTATUSDATA {
    CerfDDrawGbl          lpDD;
    Ce6_DDRAWSURFACE_LCL* lpDDSurface;
    DWORD                 dwFlags;
    HRESULT               ddRVal;
} Ce6_DDHAL_GETFLIPSTATUSDATA;

typedef struct _Ce6_DDHAL_SETPALETTEDATA {
    CerfDDrawGbl          lpDD;
    Ce6_DDRAWSURFACE_LCL* lpDDSurface;
    void*                 lpDDPalette;
    BOOL                  Attach;
    HRESULT               ddRVal;
} Ce6_DDHAL_SETPALETTEDATA;

typedef struct _Ce6_DDHAL_WAITFORVERTICALBLANKDATA {
    CerfDDrawGbl lpDD;
    DWORD        dwFlags;
    DWORD        bIsInVB;
    HRESULT      ddRVal;
} Ce6_DDHAL_WAITFORVERTICALBLANKDATA;

typedef struct _Ce6_DDHAL_CREATEPALETTEDATA {
    CerfDDrawGbl   lpDD;
    LPPALETTEENTRY lpColorTable;
    void*          lpDDPalette;
    HRESULT        ddRVal;
} Ce6_DDHAL_CREATEPALETTEDATA;

typedef struct _Ce6_DDHAL_GETDRIVERINFODATA {
    CerfDDrawGbl lpDD;
    DWORD        dwSize;
    DWORD        dwFlags;
    GUID         guidInfo;
    DWORD        dwExpectedSize;
    LPVOID       lpvData;
    DWORD        dwActualSize;
    HRESULT      ddRVal;
} Ce6_DDHAL_GETDRIVERINFODATA;

#define CE6_DDHAL_CB32_CREATESURFACE        0x00000001
#define CE6_DDHAL_CB32_WAITFORVERTICALBLANK 0x00000002
#define CE6_DDHAL_CB32_CANCREATESURFACE     0x00000004
#define CE6_DDHAL_CB32_CREATEPALETTE        0x00000008

typedef struct _Ce6_DDHAL_DDCALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    PVOID CreateSurface;
    PVOID WaitForVerticalBlank;
    PVOID CanCreateSurface;
    PVOID CreatePalette;
    PVOID GetScanLine;
} Ce6_DDHAL_DDCALLBACKS;

#define CE6_DDHAL_SURFCB32_DESTROYSURFACE     0x00000001
#define CE6_DDHAL_SURFCB32_FLIP               0x00000002
#define CE6_DDHAL_SURFCB32_LOCK               0x00000004
#define CE6_DDHAL_SURFCB32_UNLOCK             0x00000008
#define CE6_DDHAL_SURFCB32_SETCOLORKEY        0x00000010
#define CE6_DDHAL_SURFCB32_GETBLTSTATUS       0x00000020
#define CE6_DDHAL_SURFCB32_GETFLIPSTATUS      0x00000040
#define CE6_DDHAL_SURFCB32_SETPALETTE         0x00000200

typedef struct _Ce6_DDHAL_DDSURFACECALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    PVOID DestroySurface;
    PVOID Flip;
    PVOID Lock;
    PVOID Unlock;
    PVOID SetColorKey;
    PVOID GetBltStatus;
    PVOID GetFlipStatus;
    PVOID UpdateOverlay;
    PVOID SetOverlayPosition;
    PVOID SetPalette;
} Ce6_DDHAL_DDSURFACECALLBACKS;

typedef struct _Ce6_DDHAL_DDPALETTECALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    PVOID DestroyPalette;
    PVOID SetEntries;
} Ce6_DDHAL_DDPALETTECALLBACKS;

typedef struct _Ce6_DDHALINFO {
    DWORD      dwSize;
    DWORD      dwFlags;
    PVOID      lpDDCallbacks;
    PVOID      lpDDSurfaceCallbacks;
    PVOID      lpDDPaletteCallbacks;
    PVOID      GetDriverInfo;
    Ce6_DDCAPS ddCaps;
    Ce6_DDCAPS ddHelCaps;
    LPDWORD    lpdwFourCC;
} Ce6_DDHALINFO;

#pragma pack(pop)
