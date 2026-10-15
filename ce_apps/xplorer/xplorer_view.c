#include "xplorer.h"

#define MARGIN   8
#define CELL_W   84
#define CELL_H   78
#define ICON_TOP 8
#define LABEL_TOP (ICON_TOP + ICON_H + 2)

const WCHAR kViewClass[] = L"XplorerView";

struct Item {
    WCHAR    name[MAX_PATH];
    int      is_dir;
    DWORD    size;       /* low 32 bits of file size (plenty for display sort) */
    FILETIME mtime;      /* last-write time                                    */
    HICON    icon;       /* real PE icon for .exe entries, else NULL (glyph)   */
};

struct View {
    struct Item* items;
    int          count;
    int          cap;
    int          cols;
    int          content_h;
    int          scroll_y;
    int          sel;
    int          sort;   /* XSORT_*                                    */
    COLORREF     bk;     /* background fill (COLOR_WINDOW by default)   */
    WCHAR        dir[MAX_PATH];   /* current directory (for icon full paths) */
};

/* Destroy any extracted icons and clear the handles (called before re-enumerating
   and on teardown, so navigating directories doesn't leak HICONs). */
static void FreeIcons(struct View* v) {
    int i;
    for (i = 0; i < v->count; i++)
        if (v->items[i].icon) { DestroyIcon(v->items[i].icon); v->items[i].icon = NULL; }
}

/* Insert a synthetic directory item (e.g. "My Device") ahead of the enumerated
   entries, so the desktop can show it first. */
static int EnsureCap(struct View* v);
static void InsertSpecial(struct View* v, const WCHAR* name) {
    if (!EnsureCap(v)) return;
    memmove(&v->items[1], &v->items[0], (size_t)v->count * sizeof(struct Item));
    XStrCpy(v->items[0].name, name, MAX_PATH);
    v->items[0].is_dir = 1;
    v->items[0].size   = 0;
    v->items[0].icon   = NULL;
    memset(&v->items[0].mtime, 0, sizeof(FILETIME));
    v->count++;
}

static struct View* GetView(HWND h) {
    return (struct View*)GetWindowLongW(h, GWL_USERDATA);
}

/* Case-insensitive wide compare over the ASCII range (filenames). */
static int ICmp(const WCHAR* a, const WCHAR* b) {
    for (;;) {
        WCHAR ca = *a, cb = *b;
        if (ca >= L'a' && ca <= L'z') ca -= 32;
        if (cb >= L'a' && cb <= L'z') cb -= 32;
        if (ca != cb) return (int)ca - (int)cb;
        if (!ca) return 0;
        a++; b++;
    }
}

/* Extension (after the last dot), or "" if none. */
static const WCHAR* ExtOf(const WCHAR* name) {
    const WCHAR* dot = L"";
    int i;
    for (i = 0; name[i]; i++) if (name[i] == L'.') dot = name + i + 1;
    return dot;
}

/* Sort order: directories first, then `mode` within each group, name as tiebreak. */
static int ItemLess(const struct Item* a, const struct Item* b, int mode) {
    if (a->is_dir != b->is_dir) return a->is_dir ? 1 : 0;
    switch (mode) {
    case XSORT_SIZE:
        if (a->size != b->size) return a->size < b->size;
        break;
    case XSORT_DATE: {
        int c = CompareFileTime(&a->mtime, &b->mtime);
        if (c != 0) return c > 0;   /* newest first */
        break;
    }
    case XSORT_TYPE: {
        int c = ICmp(ExtOf(a->name), ExtOf(b->name));
        if (c != 0) return c < 0;
        break;
    }
    default: break;   /* XSORT_NAME */
    }
    return ICmp(a->name, b->name) < 0;
}

/* Insertion sort over the item array using the current sort mode. */
static void SortItems(struct View* v) {
    int i, j;
    for (i = 1; i < v->count; i++) {
        struct Item key = v->items[i];
        j = i - 1;
        while (j >= 0 && ItemLess(&key, &v->items[j], v->sort)) {
            v->items[j + 1] = v->items[j];
            j--;
        }
        v->items[j + 1] = key;
    }
}

static int EnsureCap(struct View* v) {
    struct Item* ni;
    int          ncap;
    if (v->count < v->cap) return 1;
    ncap = v->cap ? v->cap * 2 : 64;
    if (v->items)
        ni = (struct Item*)LocalReAlloc(v->items, (UINT)(ncap * sizeof(struct Item)),
                                        LMEM_MOVEABLE | LMEM_ZEROINIT);
    else
        ni = (struct Item*)LocalAlloc(LPTR, (UINT)(ncap * sizeof(struct Item)));
    if (!ni) return 0;
    v->items = ni;
    v->cap   = ncap;
    return 1;
}

/* Read directory `dir` into the item array, kept sorted as inserted. */
static void Enumerate(struct View* v, const WCHAR* dir) {
    WCHAR            pat[MAX_PATH];
    WIN32_FIND_DATAW wfd;
    HANDLE           h;
    int              n;

    FreeIcons(v);
    XStrCpy(v->dir, dir, MAX_PATH);
    v->count    = 0;
    v->scroll_y = 0;
    v->sel      = -1;

    n = 0;
    while (dir[n] && n < MAX_PATH - 3) { pat[n] = dir[n]; n++; }
    if (n == 0 || pat[n - 1] != L'\\') pat[n++] = L'\\';
    pat[n++] = L'*';
    pat[n]   = 0;

    h = FindFirstFileW(pat, &wfd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            struct Item it;
            XStrCpy(it.name, wfd.cFileName, MAX_PATH);
            it.is_dir = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
            it.size   = wfd.nFileSizeLow;
            it.mtime  = wfd.ftLastWriteTime;
            it.icon   = NULL;
            if (!it.is_dir && ICmp(ExtOf(it.name), L"exe") == 0) {
                WCHAR full[MAX_PATH];
                XJoinPath(v->dir, it.name, full);
                it.icon = XExtractExeIcon(full);
            }
            if (!EnsureCap(v)) break;
            v->items[v->count++] = it;
        } while (FindNextFileW(h, &wfd));
        FindClose(h);
    }
    SortItems(v);
}

static void Relayout(HWND h) {
    struct View* v = GetView(h);
    RECT         rc;
    int          rows;
    SCROLLINFO   si;

    GetClientRect(h, &rc);
    v->cols = (rc.right - MARGIN) / CELL_W;
    if (v->cols < 1) v->cols = 1;

    rows = (v->count + v->cols - 1) / v->cols;
    if (rows < 1) rows = 1;
    v->content_h = MARGIN * 2 + rows * CELL_H;

    if (v->scroll_y > v->content_h - rc.bottom) v->scroll_y = v->content_h - rc.bottom;
    if (v->scroll_y < 0) v->scroll_y = 0;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = v->content_h - 1;
    si.nPage  = rc.bottom;
    si.nPos   = v->scroll_y;
    SetScrollInfo(h, SB_VERT, &si, TRUE);
}

static void CellOrigin(struct View* v, int i, int* x, int* y) {
    int col = i % v->cols;
    int row = i / v->cols;
    *x = MARGIN + col * CELL_W;
    *y = MARGIN + row * CELL_H - v->scroll_y;
}

/* Single-line centered label, truncated with "..." when wider than the cell.
   CE 2.11 has no DT_END_ELLIPSIS, so measure with GetTextExtentPoint32W and trim.
   Uses the DC's already-set text/bk color and mode. */
static void DrawLabel(HDC dc, const WCHAR* text, RECT* rc) {
    int  avail = rc->right - rc->left;
    int  len   = lstrlenW(text);
    SIZE sz;

    GetTextExtentPoint32W(dc, text, len, &sz);
    if (sz.cx <= avail) {
        DrawTextW(dc, text, len, rc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    {
        WCHAR buf[MAX_PATH + 4];
        int   n, i;
        for (n = len - 1; n > 0; n--) {
            for (i = 0; i < n; i++) buf[i] = text[i];
            buf[n] = L'.'; buf[n + 1] = L'.'; buf[n + 2] = L'.'; buf[n + 3] = 0;
            GetTextExtentPoint32W(dc, buf, n + 3, &sz);
            if (sz.cx <= avail) break;
        }
        if (n <= 0) { buf[0] = L'.'; buf[1] = L'.'; buf[2] = L'.'; buf[3] = 0; }
        DrawTextW(dc, buf, -1, rc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void PaintView(HWND h, HDC target) {
    struct View* v = GetView(h);
    RECT         rc;
    HDC          mem;
    HBITMAP      bmp, oldbmp;
    int          i;

    GetClientRect(h, &rc);

    mem    = CreateCompatibleDC(target);
    bmp    = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    oldbmp = (HBITMAP)SelectObject(mem, bmp);

    {
        HBRUSH bg = CreateSolidBrush(v->bk);
        FillRect(mem, &rc, bg);
        DeleteObject(bg);
    }

    for (i = 0; i < v->count; i++) {
        int  x, y, icon_x;
        RECT label;

        CellOrigin(v, i, &x, &y);
        if (y + CELL_H < 0 || y > rc.bottom) continue;

        icon_x = x + (CELL_W - ICON_W) / 2;

        label.left   = x + 2;
        label.top    = y + LABEL_TOP;
        label.right  = x + CELL_W - 2;
        label.bottom = y + CELL_H - 2;

        if (i == v->sel) {
            RECT   hi;
            HBRUSH sb = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
            hi.left = x + 4; hi.top = y + 2;
            hi.right = x + CELL_W - 4; hi.bottom = y + CELL_H - 2;
            FillRect(mem, &hi, sb);
            DeleteObject(sb);
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            /* Opaque label background so labels stay readable on a dark desktop,
               like Explorer's icon-label backgrounds (no-op inside a white view). */
            SetBkMode(mem, OPAQUE);
            SetBkColor(mem, GetSysColor(COLOR_WINDOW));
            SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
        }

        if (v->items[i].icon)
            DrawIconEx(mem, icon_x, y + ICON_TOP, v->items[i].icon,
                       ICON_W, ICON_H, 0, NULL, DI_NORMAL);
        else if (v->items[i].is_dir)
            DrawFolderIcon(mem, icon_x, y + ICON_TOP);
        else
            DrawFileIcon(mem, icon_x, y + ICON_TOP);

        DrawLabel(mem, v->items[i].name, &label);
    }

    BitBlt(target, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldbmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static int HitTest(HWND h, int px, int py) {
    struct View* v = GetView(h);
    int i;
    for (i = 0; i < v->count; i++) {
        int x, y;
        CellOrigin(v, i, &x, &y);
        if (px >= x && px < x + CELL_W && py >= y && py < y + CELL_H) return i;
    }
    return -1;
}

static void OnVScroll(HWND h, WPARAM wp) {
    struct View* v = GetView(h);
    RECT         rc;
    SCROLLINFO   si;
    int          old = v->scroll_y;

    GetClientRect(h, &rc);
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(h, SB_VERT, &si);

    switch (LOWORD(wp)) {
    case SB_LINEUP:        v->scroll_y -= CELL_H;      break;
    case SB_LINEDOWN:      v->scroll_y += CELL_H;      break;
    case SB_PAGEUP:        v->scroll_y -= rc.bottom;   break;
    case SB_PAGEDOWN:      v->scroll_y += rc.bottom;   break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: v->scroll_y = si.nTrackPos; break;
    default: break;
    }

    if (v->scroll_y > v->content_h - rc.bottom) v->scroll_y = v->content_h - rc.bottom;
    if (v->scroll_y < 0) v->scroll_y = 0;

    if (v->scroll_y != old) {
        SetScrollPos(h, SB_VERT, v->scroll_y, TRUE);
        InvalidateRect(h, NULL, FALSE);
    }
}

static LRESULT CALLBACK ViewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        struct View* v = (struct View*)LocalAlloc(LPTR, sizeof(struct View));
        if (!v) return -1;
        v->sel  = -1;
        v->sort = XSORT_NAME;
        v->bk   = GetSysColor(COLOR_WINDOW);
        SetWindowLongW(h, GWL_USERDATA, (LONG)v);
        return 0;
    }

    case XVM_SETPATH: {
        struct View* v = GetView(h);
        Enumerate(v, (const WCHAR*)lp);
        Relayout(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case XVM_SETBKCOLOR: {
        struct View* v = GetView(h);
        v->bk = (COLORREF)lp;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case XVM_ADDSPECIAL: {
        struct View* v = GetView(h);
        InsertSpecial(v, (const WCHAR*)lp);
        Relayout(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case XVM_SETSORT: {
        struct View* v = GetView(h);
        v->sort = (int)wp;
        SortItems(v);
        Relayout(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_SIZE:
        Relayout(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_VSCROLL:
        OnVScroll(h, wp);
        return 0;

    case WM_LBUTTONDOWN: {
        struct View* v = GetView(h);
        int hit = HitTest(h, LOWORD(lp), HIWORD(lp));
        if (hit != v->sel) {
            v->sel = hit;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        struct View* v = GetView(h);
        int hit = HitTest(h, LOWORD(lp), HIWORD(lp));
        /* Folders descend, files launch - the frame joins this name onto the
           current path and decides what to do with it. */
        if (hit >= 0) {
            UINT n = v->items[hit].is_dir ? XVN_DESCEND : XVN_LAUNCH;
            SendMessageW(GetParent(h), n, 0, (LPARAM)v->items[hit].name);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintView(h, dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_DESTROY: {
        struct View* v = GetView(h);
        if (v) {
            FreeIcons(v);
            if (v->items) LocalFree(v->items);
            LocalFree(v);
        }
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

BOOL RegisterViewClass(HINSTANCE hi) {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_DBLCLKS;   /* needed for WM_LBUTTONDBLCLK */
    wc.lpfnWndProc   = ViewProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = kViewClass;
    return RegisterClassW(&wc) != 0;
}
