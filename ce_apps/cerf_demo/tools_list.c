#include "cerf_demo.h"
#include <commctrl.h>
#include <shellapi.h>

#define ID_TOOLS_LIST   1301
#define TOOLS_LIST_H    72
#define TOOLS_MARGIN    16
#define TOOLS_BTN_BAND  50
#define TOOLS_FONT_H    14
#define TOOLS_MASK_KEY  RGB(255, 0, 255)

typedef struct {
    const TCHAR* label;
    const TCHAR* path;
} ToolEntry;

static const ToolEntry kTools[] = {
    { TEXT("Command prompt"), TEXT("\\Windows\\cmd.exe")          },
    { TEXT("Xplorer"),        TEXT("\\CERF Storage\\xplorer.exe") },
};
#define TOOL_COUNT (sizeof(kTools) / sizeof(kTools[0]))

static HWND  g_tools_list;
static HFONT g_tools_font;
static int   g_icon_cx, g_icon_cy;
static int   g_tool_of_item[TOOL_COUNT];
static int   g_tool_items;

static int ToolPresent(const ToolEntry* t) {
    return GetFileAttributes(t->path) != 0xFFFFFFFF;
}

static HFONT MakeToolsFont(void) {
    LOGFONT lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight         = TOOLS_FONT_H;
    lf.lfWeight         = FW_NORMAL;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfQuality        = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    lstrcpy(lf.lfFaceName, TEXT("Tahoma"));
    return CreateFontIndirect(&lf);
}

static void SizeIconCell(HWND list) {
    HDC        dc = GetDC(list);
    HFONT      prev;
    SIZE       sz;
    TEXTMETRIC tm;
    int        i, cx = 0, cy;

    if (!dc) return;
    prev = (HFONT)SelectObject(dc, g_tools_font);
    for (i = 0; i < (int)TOOL_COUNT; i++) {
        GetTextExtentPoint32(dc, kTools[i].label, lstrlen(kTools[i].label), &sz);
        if (sz.cx > cx) cx = sz.cx;
    }
    GetTextMetrics(dc, &tm);
    SelectObject(dc, prev);
    ReleaseDC(list, dc);

    cx += 24;
    if (cx < g_icon_cx + 24) cx = g_icon_cx + 24;
    cy = g_icon_cy + tm.tmHeight + 16;
    SendMessage(list, LVM_SETICONSPACING, 0, MAKELONG(cx, cy));
}

static int AddToolImage(HIMAGELIST himl, const ToolEntry* t) {
    HICON   icon = NULL;
    HBITMAP bmp;
    int     image;

    ExtractIconEx(t->path, 0, &icon, NULL, 1);
    if (icon) {
        image = ImageList_ReplaceIcon(himl, -1, icon);
        DestroyIcon(icon);
        return image;
    }

    bmp = MakeGenericAppIconDdb(g_icon_cx, g_icon_cy, TOOLS_MASK_KEY);
    if (!bmp) return -1;
    image = ImageList_AddMasked(himl, bmp, TOOLS_MASK_KEY);
    DeleteObject(bmp);
    return image;
}

static void Populate(HWND list) {
    HIMAGELIST himl;
    LVITEM     item;
    int        i;

    himl = ImageList_Create(g_icon_cx, g_icon_cy, ILC_COLOR | ILC_MASK,
                            (int)TOOL_COUNT, 0);
    if (himl)
        SendMessage(list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)himl);

    g_tool_items = 0;
    for (i = 0; i < (int)TOOL_COUNT; i++) {
        int image = -1;

        if (!ToolPresent(&kTools[i])) continue;
        if (himl) image = AddToolImage(himl, &kTools[i]);

        item.mask     = LVIF_TEXT | (image >= 0 ? LVIF_IMAGE : 0);
        item.iItem    = g_tool_items;
        item.iSubItem = 0;
        item.iImage   = image;
        item.pszText  = (LPTSTR)kTools[i].label;
        SendMessage(list, LVM_INSERTITEM, 0, (LPARAM)&item);
        g_tool_of_item[g_tool_items] = i;
        g_tool_items++;
    }
}

void ToolsListCreate(HWND parent) {
    if (!EnsureCommonControls()) return;
    g_icon_cx = GetSystemMetrics(SM_CXICON);
    g_icon_cy = GetSystemMetrics(SM_CYICON);
    if (g_icon_cx < 1) g_icon_cx = 32;
    if (g_icon_cy < 1) g_icon_cy = 32;

    g_tools_list = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("SysListView32"), NULL,
        WS_CHILD | LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
        LVS_NOLABELWRAP | LVS_AUTOARRANGE | LVS_ALIGNTOP,
        0, 0, 10, 10, parent, (HMENU)ID_TOOLS_LIST, g_inst, NULL);
    if (!g_tools_list) return;
    if (!g_tools_font) g_tools_font = MakeToolsFont();
    if (g_tools_font)
        SendMessage(g_tools_list, WM_SETFONT, (WPARAM)g_tools_font, TRUE);
    SizeIconCell(g_tools_list);
    Populate(g_tools_list);
    SendMessage(g_tools_list, LVM_ARRANGE, LVA_DEFAULT, 0);
}

void ToolsListLayout(HWND parent, int expanded) {
    RECT rc;
    if (!g_tools_list) return;
    if (!expanded) {
        ShowWindow(g_tools_list, SW_HIDE);
        return;
    }
    GetClientRect(parent, &rc);
    MoveWindow(g_tools_list, TOOLS_MARGIN,
               rc.bottom - TOOLS_BTN_BAND - TOOLS_LIST_H,
               rc.right - 2 * TOOLS_MARGIN, TOOLS_LIST_H, TRUE);
    SendMessage(g_tools_list, LVM_ARRANGE, LVA_DEFAULT, 0);
    ShowWindow(g_tools_list, SW_SHOW);
}

static void LaunchTool(HWND parent, const ToolEntry* t) {
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcess(t->path, NULL, NULL, NULL, FALSE, 0,
                       NULL, NULL, NULL, &pi)) {
        MessageBox(parent, t->path, TEXT("CERF - cannot start"),
                   MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (pi.hThread)  CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
}

int ToolsListNotify(HWND parent, LPARAM lp) {
    LPNMHDR nh = (LPNMHDR)lp;
    int     sel;

    if (!g_tools_list || nh->hwndFrom != g_tools_list) return 0;
    if (nh->code != NM_DBLCLK) return 0;

    sel = (int)SendMessage(g_tools_list, LVM_GETNEXTITEM, (WPARAM)-1,
                           LVNI_SELECTED);
    if (sel >= 0 && sel < g_tool_items)
        LaunchTool(parent, &kTools[g_tool_of_item[sel]]);
    return 1;
}
