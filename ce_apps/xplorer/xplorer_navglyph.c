#include "xplorer.h"

void DrawNavGlyph(const DRAWITEMSTRUCT* dis) {
    HDC      dc       = dis->hDC;
    RECT     rc       = dis->rcItem;
    int      disabled = (dis->itemState & ODS_DISABLED) ? 1 : 0;
    int      pressed  = (!disabled && (dis->itemState & ODS_SELECTED)) ? 1 : 0;
    int      cx       = (rc.left + rc.right) / 2 + pressed;
    int      cy       = (rc.top + rc.bottom) / 2 + pressed;
    COLORREF col      = disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(0, 0, 0);
    HBRUSH   br       = CreateSolidBrush(col);
    HPEN     pen      = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ  ob, op;
    POINT    t[3];

    FillRect(dc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
    DrawEdge(dc, &rc, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);

    ob = SelectObject(dc, br);
    op = SelectObject(dc, pen);

    switch (dis->CtlID) {
    case ID_BACK:
        t[0].x = cx + 4; t[0].y = cy - 5;
        t[1].x = cx + 4; t[1].y = cy + 5;
        t[2].x = cx - 5; t[2].y = cy;
        Polygon(dc, t, 3);
        break;

    case ID_FWD:
    case ID_GO:                 /* both mean "go right" */
        t[0].x = cx - 4; t[0].y = cy - 5;
        t[1].x = cx - 4; t[1].y = cy + 5;
        t[2].x = cx + 5; t[2].y = cy;
        Polygon(dc, t, 3);
        break;

    case ID_UP:
        t[0].x = cx - 5; t[0].y = cy + 4;
        t[1].x = cx + 5; t[1].y = cy + 4;
        t[2].x = cx;     t[2].y = cy - 5;
        Polygon(dc, t, 3);
        break;

    default:                /* Refresh is a text button, not owner-draw */
        break;
    }

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}
