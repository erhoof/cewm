/* CerfDemo shared interface: cross-file globals and the surface the app
   shell (main.c) and the desktop background compositor (desktop.c) share. */
#ifndef CERF_DEMO_H
#define CERF_DEMO_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <tchar.h>

#ifndef WM_DISPLAYCHANGE
#define WM_DISPLAYCHANGE 0x007E
#endif

/* Defined in main.c, read by the background compositor. */
extern HWND  g_dlg;
extern DWORD g_start;
extern DWORD g_anim_clock;
extern int   g_fps;

extern HINSTANCE g_inst;
extern HFONT     g_ui;

/* 32bpp top-down DIB section helper (main.c). */
HBITMAP MakeDib32(int w, int h, unsigned int** bits);

int CountProcesses(void);
int CountThreads(void);
int EnsureCommonControls(void);

void BuildStats(TCHAR* buf);
int  GuestRefreshHz(void);

HBITMAP MakeGenericAppIconDdb(int w, int h, COLORREF bg);

void    ToolsListCreate(HWND parent);
void    ToolsListLayout(HWND parent, int expanded);
int     ToolsListNotify(HWND parent, LPARAM lp);

void    DrawRomsLink(HDC dc, int x, int y, HFONT link_font);
int     RomsLinkHitTest(POINT pt);
HCURSOR RomsHandCursor(void);
void    ShowRomsDialog(HWND parent, int screen_w, int screen_h);

/* Animated bokeh desktop background compositor (desktop.c). */
void InitDiscs(void);
void PresentBg(HWND h);
LRESULT CALLBACK BgProc(HWND h, UINT m, WPARAM wp, LPARAM lp);

#endif
