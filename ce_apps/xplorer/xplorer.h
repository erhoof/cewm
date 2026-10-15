#pragma once

#include <windows.h>

/* Shared metrics. */
#define TASKBAR_H 26

/* WM_DISPLAYCHANGE is absent from the CE 2.11 SDK headers (fixed-screen era), but
   0x007E is the universal Win32 message id; a CE 5.0 GWES (Zune) that broadcasts a
   resolution change sends it, so handling it gives an instant response there. The
   taskbar's metric poll is the portable backstop for systems that never send it. */
#ifndef WM_DISPLAYCHANGE
#define WM_DISPLAYCHANGE 0x007E
#endif

/* Window-to-window protocol. */
#define XVM_SETPATH   (WM_APP + 1)  /* parent->view: lParam = const WCHAR* dir; view enumerates it        */
#define XVN_DESCEND   (WM_APP + 2)  /* view->parent: lParam = const WCHAR* folder name; parent opens it   */
#define XVN_LAUNCH    (WM_APP + 3)  /* view->parent: lParam = const WCHAR* file name; parent CreateProcess*/
#define XVM_SETBKCOLOR (WM_APP + 4) /* parent->view: lParam = COLORREF background fill                    */
#define XVM_ADDSPECIAL (WM_APP + 5) /* parent->view: lParam = const WCHAR* synthetic dir item, prepended  */
#define XVM_SETSORT    (WM_APP + 6) /* parent->view: wParam = XSORT_*; re-sort the listing               */

/* View sort modes (always directories first; the mode orders within each group). */
#define XSORT_NAME 0   /* case-insensitive name, A->Z          */
#define XSORT_SIZE 1   /* file size ascending                  */
#define XSORT_DATE 2   /* last-write time, newest first        */
#define XSORT_TYPE 3   /* by extension                         */

/* Explorer frame toolbar control IDs (shared with the owner-draw glyph code). */
#define ID_BACK    101
#define ID_FWD     102
#define ID_UP      103
#define ID_REFRESH 104
#define ID_GO      105
#define ID_EDIT    106
#define ID_VIEW    107
#define ID_SORT    108

extern const WCHAR kViewClass[];
BOOL RegisterViewClass(HINSTANCE hi);

/* Icon cell size, shared by the icon drawers and the view's DrawIconEx scaling. */
#define ICON_W 32
#define ICON_H 32

/* Icons (xplorer_icons.c): generic drawn folder/file glyphs, and a real PE icon
   extracted from an .exe (NULL if unavailable - caller falls back to a glyph). */
void  DrawFolderIcon(HDC dc, int x, int y);
void  DrawFileIcon(HDC dc, int x, int y);
HICON XExtractExeIcon(const WCHAR* fullpath);

/* Owner-draw toolbar glyphs (xplorer_navglyph.c): draws a back/forward/up/refresh/go
   glyph for the given owner-draw button, sized to fit narrow toolbars. */
void DrawNavGlyph(const DRAWITEMSTRUCT* dis);

/* Desktop (xplorer_desktop.c): the primary shell surface - a bottom-most,
   non-topmost window hosting a view of \Windows\Desktop plus a "My Device"
   pseudo-folder that opens a new explorer window at the root. */
extern const WCHAR kDesktopClass[];
BOOL RegisterDesktopClass(HINSTANCE hi);
HWND CreateDesktopWindow(HINSTANCE hi);

/* Taskbar (xplorer_taskbar.c): a topmost strip pinned to the screen bottom with
   a "T" button that opens the task manager. */
extern const WCHAR kTaskbarClass[];
BOOL RegisterTaskbarClass(HINSTANCE hi);
HWND CreateTaskbar(HINSTANCE hi);

/* Task manager (xplorer_taskmgr.c): a topmost window listing top-level windows
   (EnumWindows) with owning PID + process handle and a per-row Kill. */
extern const WCHAR kTaskMgrClass[];
BOOL RegisterTaskMgrClass(HINSTANCE hi);
void ShowTaskManager(HINSTANCE hi);

/* Run dialog (xplorer_run.c): a small topmost prompt to launch a program path. */
extern const WCHAR kRunClass[];
BOOL RegisterRunClass(HINSTANCE hi);
void ShowRunDialog(HINSTANCE hi);

/* Explorer/frame (main.c): opens a new non-topmost explorer window at `path`,
   and launches an executable by full path. Shared so the desktop can drive them. */
void OpenExplorer(HINSTANCE hi, const WCHAR* path);
void LaunchExe(const WCHAR* fullpath);

/* Subdue a foreign window (drop topmost + hide) - the universal shell-coexistence
   primitive, used by the startup auto-subduer and the Task Manager's Hide button. */
void SubdueWindow(HWND w);

/* True if `w` is one of xplorer's own windows (class name begins "Xplorer"); used
   to exclude ourselves from shell-subdue and from the Task Manager list. */
static __inline int XIsOurWindow(HWND w) {
    static const WCHAR pfx[] = L"Xplorer";
    WCHAR cls[16];
    int   i;
    cls[0] = 0;
    GetClassNameW(w, cls, 16);
    for (i = 0; pfx[i]; i++) if (cls[i] != pfx[i]) return 0;
    return 1;
}

/* Bounded wide copy. CE 2.11 has lstrcpyW (== wcscpy, unbounded) but no bounded
   lstrcpynW, and every window needs a safe copy into a MAX_PATH buffer. */
static __inline void XStrCpy(WCHAR* dst, const WCHAR* src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Join "<dir>\<name>" into out (out is MAX_PATH). */
static __inline void XJoinPath(const WCHAR* dir, const WCHAR* name, WCHAR* out) {
    int n;
    XStrCpy(out, dir, MAX_PATH);
    n = lstrlenW(out);
    if (n == 0 || out[n - 1] != L'\\') { out[n++] = L'\\'; out[n] = 0; }
    XStrCpy(out + n, name, MAX_PATH - n);
}

/* Exact wide string equality. */
static __inline int XStrEq(const WCHAR* a, const WCHAR* b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}
