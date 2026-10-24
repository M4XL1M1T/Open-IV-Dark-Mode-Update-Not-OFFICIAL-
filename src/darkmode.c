/*
 * OpenIV dark mode helper.
 *
 * Loaded as a proxy "dinput8.dll" from the OpenIV folder (OpenIV.exe imports
 * dinput8.dll). On load it patches the import tables of OpenIV.exe and
 * comctl32.dll so that the colour calls of the Delphi VCL and of the native
 * common controls are mapped to a dark palette.
 *
 * Delete dinput8.dll from the OpenIV folder to get the original look back.
 *
 * Build (32-bit, e.g. llvm-mingw):
 *   i686-w64-mingw32-gcc -shared -O2 -o dinput8.dll darkmode.c -Wl,--kill-at -static -luser32 -lgdi32
 */
#include <windows.h>
#include <math.h>

/* ---------- colour transform ---------- */

static float hue2rgb(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 1.f / 2.f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

/* Lightness inversion that keeps the hue: white -> ~#1F1F1F, black -> ~#EBEBEB. */
static COLORREF xform(COLORREF c)
{
    if (c & 0xFF000000) return c; /* palette / system index flags: leave alone */

    float r = (c & 0xFF) / 255.f;
    float g = ((c >> 8) & 0xFF) / 255.f;
    float b = ((c >> 16) & 0xFF) / 255.f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
    float l = (mx + mn) / 2.f, h = 0.f, s = 0.f;

    if (mx != mn) {
        float d = mx - mn;
        s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
        if (mx == r)      h = (g - b) / d + (g < b ? 6.f : 0.f);
        else if (mx == g) h = (b - r) / d + 2.f;
        else              h = (r - g) / d + 4.f;
        h /= 6.f;
    }

    float l2 = 0.12f + (1.f - l) * 0.80f;
    s *= 0.85f;

    float nr, ng, nb;
    if (s == 0.f) {
        nr = ng = nb = l2;
    } else {
        float q = l2 < 0.5f ? l2 * (1.f + s) : l2 + s - l2 * s;
        float p = 2.f * l2 - q;
        nr = hue2rgb(p, q, h + 1.f / 3.f);
        ng = hue2rgb(p, q, h);
        nb = hue2rgb(p, q, h - 1.f / 3.f);
    }
    return RGB((BYTE)(nr * 255.f + 0.5f), (BYTE)(ng * 255.f + 0.5f), (BYTE)(nb * 255.f + 0.5f));
}

/*
 * Windows' own dark mode (enabled below for list/tree views) already makes some
 * visual-style parts dark, so flipping those again would make them light.
 * These two only act on colours that are on the "wrong" side.
 */
static int lum(COLORREF c)
{
    int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return (mx + mn) / 2;
}

static COLORREF ensure_light(COLORREF c) { return (c & 0xFF000000) || lum(c) >= 128 ? c : xform(c); }
static COLORREF ensure_dark(COLORREF c)  { return (c & 0xFF000000) || lum(c) < 128 ? c : xform(c); }

/* ---------- plain GDI colour sinks ---------- */

static COLORREF (WINAPI *oSetTextColor)(HDC, COLORREF);
static COLORREF (WINAPI *oSetBkColor)(HDC, COLORREF);
static HBRUSH   (WINAPI *oCreateSolidBrush)(COLORREF);
static HBRUSH   (WINAPI *oCreateBrushIndirect)(const LOGBRUSH *);
static HPEN     (WINAPI *oCreatePenIndirect)(const LOGPEN *);
static HPEN     (WINAPI *oExtCreatePen)(DWORD, DWORD, const LOGBRUSH *, DWORD, const DWORD *);
static HBRUSH   (WINAPI *oGetSysColorBrush)(int);
static DWORD    (WINAPI *oGetSysColor)(int);
static HGDIOBJ  (WINAPI *oGetStockObject)(int);
static BOOL     (WINAPI *oPatBlt)(HDC, int, int, int, int, DWORD);

/*
 * Pure black as a fill / background is a mask or transparency key far more
 * often than a real background, so it stays black (text and lines still flip).
 */
static COLORREF fill_color(COLORREF c) { return (c & 0x00FFFFFF) == 0 ? c : xform(c); }

static COLORREF WINAPI hSetTextColor(HDC dc, COLORREF c) { return oSetTextColor(dc, ensure_light(c)); }
static COLORREF WINAPI hSetBkColor(HDC dc, COLORREF c)   { return oSetBkColor(dc, fill_color(c)); }
static HBRUSH   WINAPI hCreateSolidBrush(COLORREF c)     { return oCreateSolidBrush(fill_color(c)); }

static HBRUSH WINAPI hCreateBrushIndirect(const LOGBRUSH *lb)
{
    if (lb && lb->lbStyle == BS_SOLID) {
        LOGBRUSH n = *lb;
        n.lbColor = fill_color(n.lbColor);
        return oCreateBrushIndirect(&n);
    }
    return oCreateBrushIndirect(lb);
}

static HPEN WINAPI hCreatePenIndirect(const LOGPEN *lp)
{
    if (lp) {
        LOGPEN n = *lp;
        n.lopnColor = xform(n.lopnColor);
        return oCreatePenIndirect(&n);
    }
    return oCreatePenIndirect(lp);
}

static HPEN WINAPI hExtCreatePen(DWORD style, DWORD w, const LOGBRUSH *lb, DWORD n, const DWORD *s)
{
    if (lb && lb->lbStyle == BS_SOLID) {
        LOGBRUSH t = *lb;
        t.lbColor = xform(t.lbColor);
        return oExtCreatePen(style, w, &t, n, s);
    }
    return oExtCreatePen(style, w, lb, n, s);
}

static HBRUSH g_sysBrush[32];
static HBRUSH g_whiteBrush;

/* System brushes must stay valid forever (callers never delete them), so cache. */
static HBRUSH WINAPI hGetSysColorBrush(int idx)
{
    if (idx < 0 || idx >= 32) return oGetSysColorBrush(idx);
    if (!g_sysBrush[idx])
        g_sysBrush[idx] = oCreateSolidBrush(xform(oGetSysColor(idx)));
    return g_sysBrush[idx] ? g_sysBrush[idx] : oGetSysColorBrush(idx);
}

static DWORD WINAPI hGetSysColor(int idx) { return xform(oGetSysColor(idx)); }

/* The stock white brush and WHITENESS fills bypass CreateSolidBrush entirely. */
static HGDIOBJ WINAPI hGetStockObject(int idx)
{
    if (idx == WHITE_BRUSH) {
        if (!g_whiteBrush) g_whiteBrush = oCreateSolidBrush(xform(RGB(255, 255, 255)));
        if (g_whiteBrush) return g_whiteBrush;
    }
    return oGetStockObject(idx);
}

static BOOL WINAPI hPatBlt(HDC dc, int x, int y, int w, int h, DWORD rop)
{
    if (rop == WHITENESS) {
        HGDIOBJ b = hGetStockObject(WHITE_BRUSH);
        RECT r = { x, y, x + w, y + h };
        if (b) return FillRect(dc, &r, (HBRUSH)b);
    }
    return oPatBlt(dc, x, y, w, h, rop);
}

/* ---------- native list / tree view colours ---------- */

#define LVM_SETBKCOLOR_     0x1001
#define LVM_GETHEADER_      0x101F
#define LVM_SETTEXTCOLOR_   0x1024
#define LVM_SETTEXTBKCOLOR_ 0x1026
#define TVM_SETBKCOLOR_     0x111D
#define TVM_SETTEXTCOLOR_   0x111E
#define TVM_SETLINECOLOR_   0x1128

static LRESULT (WINAPI *oSendMessageW)(HWND, UINT, WPARAM, LPARAM);

/* VCL pushes Color / Font.Color into the controls with these messages. */
static LRESULT WINAPI hSendMessageW(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case LVM_SETBKCOLOR_: case LVM_SETTEXTBKCOLOR_: case TVM_SETBKCOLOR_: case TVM_SETLINECOLOR_:
        if ((DWORD)l != 0xFFFFFFFF && (DWORD)l != 0xFF000000) l = (LPARAM)xform((COLORREF)l);
        break;
    case LVM_SETTEXTCOLOR_: case TVM_SETTEXTCOLOR_:
        if ((DWORD)l != 0xFFFFFFFF && (DWORD)l != 0xFF000000) l = (LPARAM)ensure_light((COLORREF)l);
        break;
    }
    return oSendMessageW(h, msg, w, l);
}

static HRESULT (WINAPI *pSetWindowTheme)(HWND, LPCWSTR, LPCWSTR);

static void dark_init_once(void)
{
    static int done;
    if (done) return;
    done = 1;
    HMODULE ux = LoadLibraryA("uxtheme.dll");
    if (!ux) return;
    pSetWindowTheme = (HRESULT(WINAPI *)(HWND, LPCWSTR, LPCWSTR))GetProcAddress(ux, "SetWindowTheme");
    int (WINAPI *setMode)(int) = (int(WINAPI *)(int))GetProcAddress(ux, MAKEINTRESOURCEA(135)); /* SetPreferredAppMode */
    void (WINAPI *flush)(void) = (void(WINAPI *)(void))GetProcAddress(ux, MAKEINTRESOURCEA(136)); /* FlushMenuThemes */
    if (setMode) setMode(2); /* ForceDark */
    if (flush) flush();
}

static void style_window(HWND h)
{
    WCHAR cls[64];
    if (!GetClassNameW(h, cls, 64) || !pSetWindowTheme) return;

    if (lstrcmpW(cls, L"TListView") == 0) {
        oSendMessageW(h, LVM_SETBKCOLOR_, 0, (LPARAM)xform(RGB(255, 255, 255)));
        oSendMessageW(h, LVM_SETTEXTBKCOLOR_, 0, (LPARAM)xform(RGB(255, 255, 255)));
        oSendMessageW(h, LVM_SETTEXTCOLOR_, 0, (LPARAM)xform(RGB(0, 0, 0)));
        pSetWindowTheme(h, L"DarkMode_Explorer", NULL);
        HWND hdr = (HWND)oSendMessageW(h, LVM_GETHEADER_, 0, 0);
        if (hdr) pSetWindowTheme(hdr, L"DarkMode_ItemsView", NULL);
    } else if (lstrcmpW(cls, L"TTreeView") == 0) {
        oSendMessageW(h, TVM_SETBKCOLOR_, 0, (LPARAM)xform(RGB(255, 255, 255)));
        oSendMessageW(h, TVM_SETTEXTCOLOR_, 0, (LPARAM)xform(RGB(0, 0, 0)));
        pSetWindowTheme(h, L"DarkMode_Explorer", NULL);
    }
}

/*
 * Title bars are drawn by Windows and follow the user's light/dark setting (or the accent colour),
 * so on a light Windows they stay white. Ask DWM for a dark caption on every top-level window.
 *   attribute 20 (19 before Windows 10 2004): immersive dark mode, Windows 10 1809+
 *   attributes 35 / 36: caption and text colour, Windows 11 only, also overrides the accent colour
 */
static void dark_title_bar(HWND h)
{
    static HRESULT (WINAPI *setAttr)(HWND, DWORD, LPCVOID, DWORD);
    if (!setAttr) {
        HMODULE m = LoadLibraryA("dwmapi.dll");
        if (m) setAttr = (HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD))GetProcAddress(m, "DwmSetWindowAttribute");
        if (!setAttr) return;
    }
    BOOL on = TRUE;
    COLORREF caption = RGB(32, 32, 32), text = RGB(235, 235, 235);
    if (FAILED(setAttr(h, 20, &on, sizeof on))) setAttr(h, 19, &on, sizeof on);
    setAttr(h, 35, &caption, sizeof caption);
    setAttr(h, 36, &text, sizeof text);
}

/*
 * The "Video tutorials" (YouTube) toolbar and the full screen button sit in the cool bar above the file list.
 * Keep them hidden: a subclass proc turns every attempt to show them into a hide.
 *   TAeroThemeButton in the cool bar panel: the full screen button
 *   TToolBar with exactly one button in the cool bar panel: the video tutorials link
 */
#define TB_BUTTONCOUNT_ 0x0418

static LRESULT CALLBACK hidden_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC old = (WNDPROC)GetPropW(h, L"DarkModeOldProc");
    WCHAR cls[64];
    int hide = 1;
    if (GetClassNameW(h, cls, 64) && lstrcmpW(cls, L"TToolBar") == 0)
        hide = CallWindowProcW(old, h, TB_BUTTONCOUNT_, 0, 0) == 1; /* not SendMessage: the VCL would call us again */
    if (hide) {
        if (m == WM_WINDOWPOSCHANGING) {
            WINDOWPOS *p = (WINDOWPOS *)l;
            p->flags = (p->flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW;
        } else if (m == WM_SHOWWINDOW && w) {
            return 0;
        }
    }
    if (m == WM_NCDESTROY) RemovePropW(h, L"DarkModeOldProc");
    return CallWindowProcW(old, h, m, w, l);
}

static void hide_cool_bar_extras(HWND wnd, HWND parent)
{
    WCHAR cls[64], pcls[64], gcls[64];
    HWND grand = parent ? GetParent(parent) : NULL;
    if (!grand || !GetClassNameW(wnd, cls, 64) || !GetClassNameW(parent, pcls, 64) || !GetClassNameW(grand, gcls, 64)) return;
    if (lstrcmpW(pcls, L"TPanel") != 0 || lstrcmpW(gcls, L"TCoolBar") != 0) return;
    if (lstrcmpW(cls, L"TAeroThemeButton") != 0 && lstrcmpW(cls, L"TToolBar") != 0) return;
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(wnd, GWLP_WNDPROC, (LONG_PTR)hidden_proc);
    SetPropW(wnd, L"DarkModeOldProc", (HANDLE)old);
}

/* ---------- message boxes ---------- */

/*
 * MessageBox is drawn entirely inside user32 (white body, grey button strip), where no colour call can
 * be intercepted. Replace it by a small dialog of our own with the same buttons, icons, default button,
 * keys and return values. Unusual flags fall back to the system dialog.
 */
typedef struct {
    const WCHAR *text, *caption;
    UINT flags;
    int nbtn, ids[4], def, result, done;
    WCHAR label[4][40];
    HWND btn[4];
    HFONT font;
    HICON icon;
    int scale100, bodyH, footerH, iconX, iconY, textX, textY;
    RECT textRc;
} DarkBox;

#define DB_CLASS L"OpenIVDarkMessageBox"


static int db_px(const DarkBox *d, int v) { return MulDiv(v, d->scale100, 100); }

static LRESULT CALLBACK db_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DarkBox *d = (DarkBox *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        d = (DarkBox *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)d);
    } else if (d) {
        switch (m) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc, body, foot;
            GetClientRect(h, &rc);
            body = rc; body.bottom = d->bodyH;
            foot = rc; foot.top = d->bodyH;
            HBRUSH b1 = CreateSolidBrush(xform(RGB(255, 255, 255))), b2 = CreateSolidBrush(xform(RGB(240, 240, 240)));
            FillRect(dc, &body, b1);
            FillRect(dc, &foot, b2);
            DeleteObject(b1); DeleteObject(b2);
            if (d->icon) DrawIconEx(dc, d->iconX, d->iconY, d->icon, 0, 0, 0, NULL, DI_NORMAL);
            HFONT of = (HFONT)SelectObject(dc, d->font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(240, 240, 240));
            RECT tr = d->textRc;
            OffsetRect(&tr, d->textX, d->textY);
            DrawTextW(dc, d->text, -1, &tr, DT_WORDBREAK | DT_NOPREFIX | DT_LEFT);
            SelectObject(dc, of);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_COMMAND:
                        if (HIWORD(w) == BN_CLICKED) {
                for (int i = 0; i < d->nbtn; i++) if (d->ids[i] == (int)LOWORD(w)) { d->result = d->ids[i]; d->done = 1; }
            }
            PostMessageW(h, WM_NULL, 0, 0); /* wake the message loop: sent messages are handled inside GetMessage */
            return 0;
        case WM_CLOSE: {
            int cancel = 0, ok = 0;
            for (int i = 0; i < d->nbtn; i++) { cancel |= d->ids[i] == IDCANCEL; ok |= d->ids[i] == IDOK; }
            if (cancel || (ok && d->nbtn == 1)) { d->result = cancel ? IDCANCEL : IDOK; d->done = 1; PostMessageW(h, WM_NULL, 0, 0); }
            return 0;
        }
        }
    }
    return DefWindowProcW(h, m, w, l);
}

static int dark_message_box(HWND owner, const WCHAR *text, const WCHAR *caption, UINT flags)
{
    static const int sets[7][4] = {
        { IDOK }, { IDOK, IDCANCEL }, { IDABORT, IDRETRY, IDIGNORE }, { IDYES, IDNO, IDCANCEL },
        { IDYES, IDNO }, { IDRETRY, IDCANCEL }, { IDCANCEL, 10 /* try again */, 11 /* continue */ } };
    static const int nsets[7] = { 1, 2, 3, 3, 2, 2, 3 };
    static const int str_id[12] = { 0, 800, 801, 802, 803, 804, 805, 806, 807, 808, 809, 810 };
    static int registered;
    DarkBox d;
    ZeroMemory(&d, sizeof d);
    UINT kind = flags & MB_TYPEMASK;
    if (kind > 6 || (flags & (MB_SERVICE_NOTIFICATION | MB_HELP))) return 0;
    if (!text) text = L"";
    if (!caption) caption = L"";
    d.text = text; d.caption = caption; d.flags = flags;
    d.nbtn = nsets[kind];
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    for (int i = 0; i < d.nbtn; i++) {
        d.ids[i] = sets[kind][i];
        d.label[i][0] = 0;
        if (d.ids[i] < 12 && str_id[d.ids[i]]) LoadStringW(u32, str_id[d.ids[i]], d.label[i], 40);
    }
    d.def = (int)((flags & MB_DEFMASK) >> 8);
    if (d.def >= d.nbtn) d.def = 0;
    switch (flags & MB_ICONMASK) {
    case MB_ICONERROR:       d.icon = LoadIconW(NULL, (LPCWSTR)IDI_ERROR); break;
    case MB_ICONQUESTION:    d.icon = LoadIconW(NULL, (LPCWSTR)IDI_QUESTION); break;
    case MB_ICONWARNING:     d.icon = LoadIconW(NULL, (LPCWSTR)IDI_WARNING); break;
    case MB_ICONINFORMATION: d.icon = LoadIconW(NULL, (LPCWSTR)IDI_INFORMATION); break;
    }

    dark_init_once();
    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = db_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = DB_CLASS;
        if (!RegisterClassExW(&wc)) return 0;
        registered = 1;
    }
    if (owner && !IsWindow(owner)) owner = NULL;
    if (!owner) owner = GetActiveWindow();
    if (owner) owner = GetAncestor(owner, GA_ROOT);

    HDC sdc = GetDC(NULL);
    d.scale100 = MulDiv(GetDeviceCaps(sdc, LOGPIXELSX), 100, 96);
    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    d.font = CreateFontIndirectW(&ncm.lfMessageFont);
    HFONT of = (HFONT)SelectObject(sdc, d.font);

    int margin = db_px(&d, 20), gap = db_px(&d, 14), bh = db_px(&d, 26), bgap = db_px(&d, 8), maxw = db_px(&d, 420);
    int iconW = d.icon ? GetSystemMetrics(SM_CXICON) : 0, iconH = d.icon ? GetSystemMetrics(SM_CYICON) : 0;
    int textLeft = margin + (iconW ? iconW + gap : 0);
    RECT tr = { 0, 0, maxw - textLeft - margin, 0 };
    DrawTextW(sdc, text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_LEFT);
    d.textRc = tr;

    int bw[4], btotal = 0;
    for (int i = 0; i < d.nbtn; i++) {
        RECT br = { 0, 0, 0, 0 };
        DrawTextW(sdc, d.label[i], -1, &br, DT_CALCRECT | DT_SINGLELINE);
        bw[i] = br.right - br.left + db_px(&d, 28);
        if (bw[i] < db_px(&d, 80)) bw[i] = db_px(&d, 80);
        btotal += bw[i] + (i ? bgap : 0);
    }
    SIZE cs = { 0, 0 };
    GetTextExtentPoint32W(sdc, caption, lstrlenW(caption), &cs);
    SelectObject(sdc, of);
    ReleaseDC(NULL, sdc);

    int textH = tr.bottom - tr.top, blockH = textH > iconH ? textH : iconH;
    int cw = textLeft + (tr.right - tr.left) + margin;
    if (cw < btotal + 2 * margin) cw = btotal + 2 * margin;
    if (cw < cs.cx + db_px(&d, 90)) cw = cs.cx + db_px(&d, 90);
    if (cw < db_px(&d, 240)) cw = db_px(&d, 240);
    d.bodyH = margin + blockH + margin;
    d.footerH = db_px(&d, 14) + bh + db_px(&d, 14);
    d.iconX = margin; d.iconY = margin;
    d.textX = textLeft; d.textY = margin + (iconH > textH ? (iconH - textH) / 2 : 0);
    int ch = d.bodyH + d.footerH;

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    if (flags & (MB_TOPMOST | MB_SYSTEMMODAL)) ex |= WS_EX_TOPMOST;
    RECT wr = { 0, 0, cw, ch };
    AdjustWindowRectEx(&wr, style, FALSE, ex);
    RECT area;
    if (owner && IsWindowVisible(owner) && !IsIconic(owner)) GetWindowRect(owner, &area);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    int wx = area.left + (area.right - area.left - ww) / 2, wy = area.top + (area.bottom - area.top - wh) / 2;
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (wx + ww > wa.right) wx = wa.right - ww;
    if (wy + wh > wa.bottom) wy = wa.bottom - wh;
    if (wx < wa.left) wx = wa.left;
    if (wy < wa.top) wy = wa.top;

    HWND dlg = CreateWindowExW(ex, DB_CLASS, caption, style, wx, wy, ww, wh, owner, NULL, GetModuleHandleW(NULL), &d);
    if (!dlg) { DeleteObject(d.font); return 0; }
    dark_title_bar(dlg);

    int x = (cw - btotal) / 2, y = d.bodyH + db_px(&d, 14);
    for (int i = 0; i < d.nbtn; i++) {
        d.btn[i] = CreateWindowExW(0, L"BUTTON", d.label[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == d.def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                                   x, y, bw[i], bh, dlg, (HMENU)(INT_PTR)d.ids[i], GetModuleHandleW(NULL), NULL);
        SendMessageW(d.btn[i], WM_SETFONT, (WPARAM)d.font, TRUE);
        if (pSetWindowTheme) pSetWindowTheme(d.btn[i], L"DarkMode_Explorer", NULL);
        x += bw[i] + bgap;
    }
    int cancel = 0;
    for (int i = 0; i < d.nbtn; i++) cancel |= d.ids[i] == IDCANCEL;
    if (!cancel && !(d.nbtn == 1 && d.ids[0] == IDOK)) EnableMenuItem(GetSystemMenu(dlg, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);

    if (owner) EnableWindow(owner, FALSE);
    MessageBeep(flags & MB_ICONMASK);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(d.btn[d.def]);

    MSG msg;
    while (!d.done) {
        if (!GetMessageW(&msg, NULL, 0, 0)) { PostQuitMessage((int)msg.wParam); d.result = IDCANCEL; break; }
        if (msg.message == WM_KEYDOWN && (msg.hwnd == dlg || IsChild(dlg, msg.hwnd))) {
            if (msg.wParam == VK_RETURN) {
                HWND f = GetFocus();
                int id = d.ids[d.def];
                for (int i = 0; i < d.nbtn; i++) if (d.btn[i] == f) id = d.ids[i];
                d.result = id; d.done = 1;
                continue;
            }
            if (msg.wParam == VK_ESCAPE) { SendMessageW(dlg, WM_CLOSE, 0, 0); continue; }
        }
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    DeleteObject(d.font);
    return d.result;
}

static int (WINAPI *oMessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT);
static int (WINAPI *oMessageBoxA)(HWND, LPCSTR, LPCSTR, UINT);

static int WINAPI hMessageBoxW(HWND o, LPCWSTR text, LPCWSTR caption, UINT flags)
{
    int r = dark_message_box(o, text, caption, flags);
    return r ? r : oMessageBoxW(o, text, caption, flags);
}

static WCHAR *widen(LPCSTR a)
{
    if (!a) return NULL;
    int n = MultiByteToWideChar(CP_ACP, 0, a, -1, NULL, 0);
    WCHAR *w = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(n ? n : 1) * sizeof(WCHAR));
    if (w) { w[0] = 0; MultiByteToWideChar(CP_ACP, 0, a, -1, w, n); }
    return w;
}

static int WINAPI hMessageBoxA(HWND o, LPCSTR text, LPCSTR caption, UINT flags)
{
    WCHAR *t = widen(text), *c = widen(caption);
    int r = (t || !text) ? dark_message_box(o, t, c, flags) : 0;
    if (t) HeapFree(GetProcessHeap(), 0, t);
    if (c) HeapFree(GetProcessHeap(), 0, c);
    return r ? r : oMessageBoxA(o, text, caption, flags);
}

/* Style each list / tree view right after the VCL created its window (also after every re-creation). */
static HWND (WINAPI *oCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);

static HWND WINAPI hCreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h,
                                    HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    HWND wnd = oCreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (wnd && !(style & WS_CHILD)) dark_title_bar(wnd);
    if (wnd && HIWORD(cls) != 0) { /* a string class name, not an atom */
        dark_init_once();
        style_window(wnd);
        if (style & WS_CHILD) hide_cool_bar_extras(wnd, parent);
    }
    return wnd;
}

/* ---------- pixel filter for parts we cannot recolour through a colour call ---------- */

/* Snapshot of a DC rectangle, so only pixels that were actually painted get darkened. */
typedef struct { HDC m; HBITMAP bm, old; DWORD *px, *before; int x, y, w, h; } Snap;

static int snap_begin(HDC dc, const RECT *r, const RECT *clip, Snap *s)
{
    RECT q = *r;
    if (clip) {
        if (q.left < clip->left) q.left = clip->left;
        if (q.top < clip->top) q.top = clip->top;
        if (q.right > clip->right) q.right = clip->right;
        if (q.bottom > clip->bottom) q.bottom = clip->bottom;
    }
    s->x = q.left; s->y = q.top; s->w = q.right - q.left; s->h = q.bottom - q.top;
    s->m = NULL; s->before = NULL;
    if (s->w <= 0 || s->h <= 0 || (long long)s->w * s->h > 4000000) return 0;
    if (GetDeviceCaps(dc, TECHNOLOGY) != DT_RASDISPLAY) return 0;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = s->w;
    bi.bmiHeader.biHeight = -s->h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    s->m = CreateCompatibleDC(dc);
    s->bm = s->m ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&s->px, NULL, 0) : NULL;
    if (!s->bm) { if (s->m) DeleteDC(s->m); s->m = NULL; return 0; }
    s->old = (HBITMAP)SelectObject(s->m, s->bm);
    s->before = (DWORD *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)s->w * s->h * 4);
    if (!s->before || !BitBlt(s->m, 0, 0, s->w, s->h, dc, s->x, s->y, SRCCOPY)) {
        if (s->before) HeapFree(GetProcessHeap(), 0, s->before);
        s->before = NULL;
        SelectObject(s->m, s->old); DeleteObject(s->bm); DeleteDC(s->m); s->m = NULL;
        return 0;
    }
    CopyMemory(s->before, s->px, (SIZE_T)s->w * s->h * 4);
    return 1;
}

static void snap_free(Snap *s)
{
    if (!s->m) return;
    HeapFree(GetProcessHeap(), 0, s->before);
    SelectObject(s->m, s->old); DeleteObject(s->bm); DeleteDC(s->m);
    s->m = NULL;
}

/* Write the (already edited) pixels back and release the snapshot. */
static void snap_commit(HDC dc, Snap *s)
{
    if (!s->m) return;
    BitBlt(dc, s->x, s->y, s->w, s->h, s->m, 0, 0, SRCCOPY);
    snap_free(s);
}

static void snap_end(HDC dc, Snap *s)
{
    if (!s->m) return;
    if (BitBlt(s->m, 0, 0, s->w, s->h, dc, s->x, s->y, SRCCOPY)) {
        DWORD last_in = 0xFFFFFFFF, last_out = 0;
        int changed = 0;
        for (long long i = 0, n = (long long)s->w * s->h; i < n; i++) {
            DWORD p = s->px[i];
            if (((p ^ s->before[i]) & 0x00FFFFFF) == 0) continue;
            changed = 1;
            if ((p & 0x00FFFFFF) != last_in) {
                last_in = p & 0x00FFFFFF;
                COLORREF c = ensure_dark(RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF));
                last_out = ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);
            }
            s->px[i] = (p & 0xFF000000) | last_out;
        }
        if (changed) BitBlt(dc, s->x, s->y, s->w, s->h, s->m, 0, 0, SRCCOPY);
    }
    snap_free(s);
}

/* Classic 3D edges / frame controls take their colours inside user32. */
static BOOL (WINAPI *oDrawEdge)(HDC, LPRECT, UINT, UINT);
static BOOL (WINAPI *oDrawFrameControl)(HDC, LPRECT, UINT, UINT);

static BOOL WINAPI hDrawEdge(HDC dc, LPRECT r, UINT edge, UINT flags)
{
    Snap s;
    int ok = r && snap_begin(dc, r, NULL, &s);
    BOOL res = oDrawEdge(dc, r, edge, flags);
    if (ok) snap_end(dc, &s);
    return res;
}

static BOOL WINAPI hDrawFrameControl(HDC dc, LPRECT r, UINT type, UINT state)
{
    Snap s;
    int ok = r && snap_begin(dc, r, NULL, &s);
    BOOL res = oDrawFrameControl(dc, r, type, state);
    if (ok) snap_end(dc, &s);
    return res;
}

/*
 * The list view draws its group headers ("Folder (8)") in the light theme's
 * navy #1E3287 straight from uxtheme, where no colour call can be intercepted.
 * Recolour exactly those pixels. Text is ClearType-blended with the dark list
 * background, so the coverage is estimated from the blue channel and the pixel
 * has to lie on the bg -> navy line (this skips selection colours and icons).
 */
static int recolor_group_text(DWORD *p, int bg)
{
    int r = (*p >> 16) & 0xFF, g = (*p >> 8) & 0xFF, b = *p & 0xFF;
    if (b < bg + 8) return 0;
    float t = (b - bg) / 104.f;                    /* navy is 104 steps above the background in blue */
    if (t > 1.15f) return 0;
    if (abs(r - bg) > 4 || fabsf(g - (bg + 19.f * t)) > 12.f) return 0;
    if (t > 1.f) t = 1.f;
    *p = (*p & 0xFF000000) | ((DWORD)(bg + t * (117 - bg) + .5f) << 16) | ((DWORD)(bg + t * (160 - bg) + .5f) << 8) |
         (DWORD)(bg + t * (255 - bg) + .5f);
    return 1;
}

/*
 * Task dialogs (DirectUI) fill their white body and grey button strip straight into a bitmap, so no colour
 * call can be intercepted. Darken the result after painting instead:
 *   - neutral light pixels (white, #F0F0F0) and the black text anti-aliasing next to them are inverted,
 *   - coloured pixels (blue text, the icon) are left alone, and so are large dark neutral blocks that are
 *     already dark (buttons, command links), together with a 1 px ring around them.
 */
static int dui_neutral(DWORD p)
{
    int r = p >> 16 & 0xFF, g = p >> 8 & 0xFF, b = p & 0xFF;
    int hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return hi - lo <= 6;
}

static int dui_lum(DWORD p) { return ((p >> 16 & 0xFF) + (p >> 8 & 0xFF) + (p & 0xFF)) / 3; }

/* Mark the 4-connected component containing `start` (all pixels where match(px) is true); returns its size and bounds. */
static int dui_component(const DWORD *px, int w, int h, int start, int mode, unsigned char *seen, int *stack, RECT *bb)
{
    int n = 0, top = 0;
    stack[top++] = start; seen[start] = 1;
    bb->left = bb->right = start % w; bb->top = bb->bottom = start / w;
    while (top) {
        int i = stack[--top], x = i % w, y = i / w;
        n++;
        if (x < bb->left) bb->left = x;
        if (x > bb->right) bb->right = x;
        if (y < bb->top) bb->top = y;
        if (y > bb->bottom) bb->bottom = y;
        int nb[4] = { x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1 };
        for (int k = 0; k < 4; k++) {
            int j = nb[k];
            if (j < 0 || seen[j]) continue;
            DWORD p = px[j] & 0xFFFFFF;
            int ok = mode == 0 ? (!dui_neutral(p))               /* coloured: icons, blue text */
                              : (dui_neutral(p) && dui_lum(p) < 100); /* dark neutral: buttons, black text */
            if (ok) { seen[j] = 1; stack[top++] = j; }
        }
    }
    return n;
}

static void dui_darken(HDC dc, const RECT *rc)
{
    Snap s;
    if (!snap_begin(dc, rc, NULL, &s)) return;
    int w = s.w, h = s.h;
    long long n = (long long)w * h;
    if (n > 600000) { snap_free(&s); return; }
    unsigned char *prot = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)n * 3);
    int *stack = (int *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(int));
    if (!prot || !stack) { if (prot) HeapFree(GetProcessHeap(), 0, prot); if (stack) HeapFree(GetProcessHeap(), 0, stack); snap_free(&s); return; }
    unsigned char *seen = prot + n, *light = prot + 2 * n;
    unsigned char lut[256];
    for (int v = 0; v < 256; v++) lut[v] = GetRValue(xform(RGB(v, v, v)));
    int dark = lut[255];

#define LIGHTN(i) (dui_neutral(s.px[i] & 0xFFFFFF) && (s.px[i] & 0xFF) >= 170)

    /* protect icons (prot = 2) and already dark blocks (prot = 1) */
    RECT icons[8];
    int nicons = 0;
    for (int mode = 0; mode < 2; mode++) {
        ZeroMemory(seen, (SIZE_T)n);
        for (int i = 0; i < n; i++) {
            DWORD p = s.px[i] & 0xFFFFFF;
            int start = mode == 0 ? !dui_neutral(p) : (dui_neutral(p) && dui_lum(p) < 100);
            if (!start || seen[i]) continue;
            RECT bb;
            int cnt = dui_component(s.px, w, h, i, mode, seen, stack, &bb);
            int bw = bb.right - bb.left + 1, bh = bb.bottom - bb.top + 1;
            int keep = mode == 0 ? (bw >= 16 && bh >= 16 && bw <= 80 && bh <= 80 && cnt * 100 / (bw * bh) >= 40) /* a filled icon, not a letter or a frame */
                                 : (cnt >= 400);                                                                 /* an already dark block */
            if (!keep) continue;
            if (mode == 0 && nicons < 8) {
                icons[nicons].left = bb.left > 0 ? bb.left - 1 : 0;
                icons[nicons].top = bb.top > 0 ? bb.top - 1 : 0;
                icons[nicons].right = bb.right < w - 1 ? bb.right + 1 : w - 1;
                icons[nicons].bottom = bb.bottom < h - 1 ? bb.bottom + 1 : h - 1;
                nicons++;
            }
            for (int y = bb.top - 1; y <= bb.bottom + 1; y++)
                for (int x = bb.left - 1; x <= bb.right + 1; x++)
                    if (x >= 0 && y >= 0 && x < w && y < h) { if (mode == 1) prot[y * w + x] = 1; else if (!prot[y * w + x]) prot[y * w + x] = 2; }
        }
    }

    /* light background: everywhere outside protection; inside an icon box only what touches the box edge */
    for (int i = 0; i < n; i++) light[i] = prot[i] == 0 && LIGHTN(i);
    for (int k = 0; k < nicons; k++) {
        RECT q = icons[k];
        int top = 0;
        for (int y = q.top; y <= q.bottom; y++)
            for (int x = q.left; x <= q.right; x++) {
                int i = y * w + x;
                if ((x == q.left || x == q.right || y == q.top || y == q.bottom) && prot[i] == 2 && LIGHTN(i) && !light[i]) { light[i] = 1; stack[top++] = i; }
            }
        while (top) {
            int i = stack[--top], x = i % w, y = i / w;
            int nb[4] = { x > q.left ? i - 1 : -1, x < q.right ? i + 1 : -1, y > q.top ? i - w : -1, y < q.bottom ? i + w : -1 };
            for (int m = 0; m < 4; m++) {
                int j = nb[m];
                if (j >= 0 && !light[j] && prot[j] == 2 && LIGHTN(j)) { light[j] = 1; stack[top++] = j; }
            }
        }
    }

    /* recolour into a copy of the decisions, so neighbours are judged on the original pixels */
    DWORD *out = (DWORD *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(DWORD));
    if (out) {
        CopyMemory(out, s.px, (SIZE_T)n * sizeof(DWORD));
        int changed = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int i = y * w + x;
                DWORD p = s.px[i];
                if (prot[i] == 1) continue;
                int near_light = light[i];
                for (int dy = -2; dy <= 2 && !near_light; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx >= 0 && yy >= 0 && xx < w && yy < h && light[yy * w + xx]) { near_light = 1; break; }
                    }
                if (!near_light) continue;
                if (dui_neutral(p & 0xFFFFFF)) {
                    if (prot[i] == 2 && !light[i]) continue;
                    unsigned v = lut[p & 0xFF];
                    out[i] = (p & 0xFF000000) | (v << 16) | (v << 8) | v;
                    changed = 1;
                } else {
                    /* anti-aliased edge of coloured text on the light background: take the white share out of the blend */
                    int tc = 255;
                    for (int dy = -2; dy <= 2; dy++)
                        for (int dx = -2; dx <= 2; dx++) {
                            int xx = x + dx, yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= w || yy >= h || prot[yy * w + xx] == 1) continue;
                            DWORD q = s.px[yy * w + xx];
                            if (dui_neutral(q & 0xFFFFFF)) continue;
                            int mn = (int)(q >> 16 & 0xFF) < (int)(q >> 8 & 0xFF) ? (int)(q >> 16 & 0xFF) : (int)(q >> 8 & 0xFF);
                            if ((int)(q & 0xFF) < mn) mn = (int)(q & 0xFF);
                            if (mn < tc) tc = mn;
                        }
                    int r = p >> 16 & 0xFF, g = p >> 8 & 0xFF, b = p & 0xFF;
                    int t = r < g ? (r < b ? r : b) : (g < b ? g : b);
                    if (t > tc && tc < 255) {
                        int sub = (t - tc) * (255 - dark) / (255 - tc);
                        r -= sub; g -= sub; b -= sub;
                        if (r < 0) r = 0;
                        if (g < 0) g = 0;
                        if (b < 0) b = 0;
                        out[i] = (p & 0xFF000000) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
                        changed = 1;
                    }
                }
            }
        }
        if (changed) { CopyMemory(s.px, out, (SIZE_T)n * sizeof(DWORD)); snap_commit(dc, &s); } else snap_free(&s);
        HeapFree(GetProcessHeap(), 0, out);
    } else {
        snap_free(&s);
    }
    HeapFree(GetProcessHeap(), 0, prot);
    HeapFree(GetProcessHeap(), 0, stack);
#undef LIGHTN
}

/* Post-paint fix for the list view's group header text. */
static BOOL (WINAPI *oEndPaint)(HWND, const PAINTSTRUCT *);

static BOOL WINAPI hEndPaint(HWND h, const PAINTSTRUCT *ps)
{
    WCHAR cls[64];
    if (ps && GetClassNameW(h, cls, 64) && lstrcmpW(cls, L"TListView") == 0) {
        Snap s;
        if (snap_begin(ps->hdc, &ps->rcPaint, NULL, &s)) {
            int bg = GetRValue(xform(RGB(255, 255, 255))), changed = 0;
            for (long long i = 0, n = (long long)s.w * s.h; i < n; i++) changed |= recolor_group_text(&s.px[i], bg);
            if (changed) snap_commit(ps->hdc, &s); else snap_free(&s);
        }
    }
    else if (ps && GetClassNameW(h, cls, 64) && lstrcmpW(cls, L"DirectUIHWND") == 0) dui_darken(ps->hdc, &ps->rcPaint);
    return oEndPaint(h, ps);
}

/* ---------- visual styles (uxtheme) + gradients, bound via GetProcAddress / delay load ---------- */

typedef HANDLE HTHEME_;

typedef struct {
    DWORD dwSize, dwFlags;
    COLORREF crText, crBorder, crShadow;
    int iTextShadowType;
    POINT ptShadowOffset;
    int iBorderSize, iFontPropId, iColorPropId, iStateId;
    BOOL fApplyOverlay;
    int iGlowSize;
    void *pfnDrawTextCallback;
    LPARAM lParam;
} DTTOPTS_;

#define DTT_TEXTCOLOR_ 0x1
#define TMT_TEXTCOLOR_ 3803

static HRESULT  (WINAPI *oDrawThemeBackground)(HTHEME_, HDC, int, int, const RECT *, const RECT *);
static HRESULT  (WINAPI *oDrawThemeEdge)(HTHEME_, HDC, int, int, const RECT *, UINT, UINT, RECT *);
static HRESULT  (WINAPI *oDrawThemeParentBackground)(HWND, HDC, const RECT *);
static HRESULT  (WINAPI *oDrawThemeText)(HTHEME_, HDC, int, int, LPCWSTR, int, DWORD, DWORD, const RECT *);
static HRESULT  (WINAPI *oDrawThemeTextEx)(HTHEME_, HDC, int, int, LPCWSTR, int, DWORD, RECT *, const DTTOPTS_ *);
static HRESULT  (WINAPI *oGetThemeColor)(HTHEME_, int, int, int, COLORREF *);
static COLORREF (WINAPI *oGetThemeSysColor)(HTHEME_, int);
static BOOL     (WINAPI *oGradientFill)(HDC, TRIVERTEX *, ULONG, PVOID, ULONG, ULONG);

/*
 * The drop-down arrow of the toolbar's split buttons is drawn by the theme as a flat neutral dark grey
 * (#282828), which vanishes on the dark toolbar and on its blue hover fill. It is recognised as a small
 * group (6 to 60 pixels) of one neutral dark colour; the backgrounds are either larger or tinted.
 * Anti-aliased chevrons consist of many colours and never form such a group.
 */
static int neutral_dark(DWORD c)
{
    int r = c >> 16 & 0xFF, g = c >> 8 & 0xFF, b = c & 0xFF;
    return r == g && g == b && r < 100;
}

static void brighten_flat_glyph(HDC dc, const RECT *r)
{
    Snap s;
    if (!snap_begin(dc, r, NULL, &s)) return;
    long long n = (long long)s.w * s.h;
    int changed = 0;
    if (n <= 1000) {
        for (long long i = 0; i < n; i++) {
            DWORD c = s.px[i] & 0x00FFFFFF;
            if (!neutral_dark(c) || c == 0x00C0C0C0) continue;
            long long same = 0;
            for (long long j = 0; j < n; j++) same += (s.px[j] & 0x00FFFFFF) == c;
            if (same >= 6 && same <= 60) {
                for (long long j = 0; j < n; j++) if ((s.px[j] & 0x00FFFFFF) == c) s.px[j] = (s.px[j] & 0xFF000000) | 0x00C0C0C0;
                changed = 1;
            }
        }
    }
    if (changed) snap_commit(dc, &s); else snap_free(&s);
}

static HRESULT WINAPI hDrawThemeBackground(HTHEME_ th, HDC dc, int part, int state, const RECT *r, const RECT *clip)
{
    Snap s;
    int ok = r && snap_begin(dc, r, clip, &s);
    HRESULT hr = oDrawThemeBackground(th, dc, part, state, r, clip);
    if (ok) snap_end(dc, &s);
    if (r && part == 4 && r->right - r->left <= 20 && r->bottom - r->top <= 30) brighten_flat_glyph(dc, r);
    return hr;
}

static HRESULT WINAPI hDrawThemeEdge(HTHEME_ th, HDC dc, int part, int state, const RECT *r, UINT edge, UINT flags, RECT *content)
{
    Snap s;
    int ok = r && snap_begin(dc, r, NULL, &s);
    HRESULT hr = oDrawThemeEdge(th, dc, part, state, r, edge, flags, content);
    if (ok) snap_end(dc, &s);
    return hr;
}

static HRESULT WINAPI hDrawThemeParentBackground(HWND h, HDC dc, const RECT *r)
{
    RECT rc;
    if (!r) { GetClientRect(h, &rc); r = &rc; }
    Snap s;
    int ok = snap_begin(dc, r, NULL, &s);
    HRESULT hr = oDrawThemeParentBackground(h, dc, r);
    if (ok) snap_end(dc, &s);
    return hr;
}

static COLORREF theme_text_color(HTHEME_ th, int part, int state)
{
    COLORREF tc = 0;
    if (!(oGetThemeColor && SUCCEEDED(oGetThemeColor(th, part, state, TMT_TEXTCOLOR_, &tc)))) tc = 0;
    return ensure_light(tc);
}

static HRESULT WINAPI hDrawThemeTextEx(HTHEME_ th, HDC dc, int part, int state, LPCWSTR txt, int cch, DWORD flags, RECT *r, const DTTOPTS_ *o)
{
    DTTOPTS_ n;
    ZeroMemory(&n, sizeof n);
    if (o) CopyMemory(&n, o, o->dwSize < sizeof n ? o->dwSize : sizeof n);
    n.dwSize = sizeof n;
    if (n.dwFlags & DTT_TEXTCOLOR_) n.crText = ensure_light(n.crText);
    else { n.dwFlags |= DTT_TEXTCOLOR_; n.crText = theme_text_color(th, part, state); }
    /* The white glow / shadow / outline that keeps dark text readable turns into a white box on dark. */
    n.dwFlags &= ~0x83Eu; /* BORDERCOLOR | SHADOWCOLOR | SHADOWTYPE | SHADOWOFFSET | BORDERSIZE | GLOWSIZE */
    n.iGlowSize = 0;
    n.dwFlags &= ~0x2000u; /* DTT_COMPOSITED: needs the glow path to render correctly on a dark background */
    return oDrawThemeTextEx(th, dc, part, state, txt, cch, flags, r, &n);
}

static HRESULT WINAPI hDrawThemeText(HTHEME_ th, HDC dc, int part, int state, LPCWSTR txt, int cch, DWORD flags, DWORD flags2, const RECT *r)
{
    (void)flags2;
    DTTOPTS_ n;    ZeroMemory(&n, sizeof n);
    n.dwSize = sizeof n;
    n.dwFlags = DTT_TEXTCOLOR_;
    n.crText = theme_text_color(th, part, state);
    return oDrawThemeTextEx(th, dc, part, state, txt, cch, flags, (RECT *)r, &n);
}

static HRESULT WINAPI hGetThemeColor(HTHEME_ th, int part, int state, int prop, COLORREF *out)
{
    HRESULT hr = oGetThemeColor(th, part, state, prop, out);
    if (SUCCEEDED(hr) && out && prop == TMT_TEXTCOLOR_) *out = ensure_light(*out);
    return hr;
}

static COLORREF WINAPI hGetThemeSysColor(HTHEME_ th, int id) { return xform(oGetThemeSysColor(th, id)); }

static BOOL WINAPI hGradientFill(HDC dc, TRIVERTEX *v, ULONG n, PVOID mesh, ULONG nm, ULONG mode)
{
    TRIVERTEX *c = (TRIVERTEX *)HeapAlloc(GetProcessHeap(), 0, n * sizeof(TRIVERTEX));
    if (!c) return oGradientFill(dc, v, n, mesh, nm, mode);
    CopyMemory(c, v, n * sizeof(TRIVERTEX));
    for (ULONG i = 0; i < n; i++) {
        COLORREF x = xform(RGB(c[i].Red >> 8, c[i].Green >> 8, c[i].Blue >> 8));
        c[i].Red = (COLOR16)(GetRValue(x) << 8);
        c[i].Green = (COLOR16)(GetGValue(x) << 8);
        c[i].Blue = (COLOR16)(GetBValue(x) << 8);
    }
    BOOL ok = oGradientFill(dc, c, n, mesh, nm, mode);
    HeapFree(GetProcessHeap(), 0, c);
    return ok;
}

static const struct { const char *name; void **orig; void *hook; } g_misc[] = {
    { "DrawThemeBackground",       (void **)&oDrawThemeBackground,       hDrawThemeBackground },
    { "DrawThemeEdge",             (void **)&oDrawThemeEdge,             hDrawThemeEdge },
    { "DrawThemeParentBackground", (void **)&oDrawThemeParentBackground, hDrawThemeParentBackground },
    { "DrawThemeText",             (void **)&oDrawThemeText,             hDrawThemeText },
    { "DrawThemeTextEx",           (void **)&oDrawThemeTextEx,           hDrawThemeTextEx },
    { "GetThemeColor",             (void **)&oGetThemeColor,             hGetThemeColor },
    { "GetThemeSysColor",          (void **)&oGetThemeSysColor,          hGetThemeSysColor },
    { "GradientFill",              (void **)&oGradientFill,              hGradientFill },
};

static FARPROC (WINAPI *oGetProcAddress)(HMODULE, LPCSTR);

/* The exe resolves uxtheme / msimg32 functions at run time. */
static FARPROC WINAPI hGetProcAddress(HMODULE m, LPCSTR name)
{
    FARPROC p = oGetProcAddress(m, name);
    if (p && HIWORD(name) != 0 && (m == GetModuleHandleA("uxtheme.dll") || m == GetModuleHandleA("msimg32.dll"))) {
        for (unsigned i = 0; i < sizeof g_misc / sizeof g_misc[0]; i++) {
            if (lstrcmpA(name, g_misc[i].name) == 0) {
                *g_misc[i].orig = (void *)p;
                return (FARPROC)g_misc[i].hook;
            }
        }
    }
    return p;
}

/* ---------- import table patching ---------- */

static void patch_iat(HMODULE mod, const char *dll, const char *fn, void *repl)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;

    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        if (lstrcmpiA((const char *)(base + d->Name), dll) != 0) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *addrs = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (lstrcmpA((const char *)ibn->Name, fn) != 0) continue;
            DWORD old;
            if (VirtualProtect(&addrs->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) {
                addrs->u1.Function = (DWORD_PTR)repl;
                VirtualProtect(&addrs->u1.Function, sizeof(void *), old, &old);
            }
        }
    }
}

/* Delay-load imports (comctl32 pulls uxtheme in this way). */
typedef struct {
    DWORD Attributes, DllNameRVA, ModuleHandleRVA, ImportAddressTableRVA, ImportNameTableRVA,
          BoundImportAddressTableRVA, UnloadInformationTableRVA, TimeDateStamp;
} DelayDesc;

static void patch_delay_iat(HMODULE mod, const char *dll, const char *fn, void *repl)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (!dir.VirtualAddress) return;

    for (DelayDesc *d = (DelayDesc *)(base + dir.VirtualAddress); d->DllNameRVA; d++) {
        if (lstrcmpiA((const char *)(base + d->DllNameRVA), dll) != 0) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->ImportNameTableRVA);
        IMAGE_THUNK_DATA *addrs = (IMAGE_THUNK_DATA *)(base + d->ImportAddressTableRVA);
        for (; names->u1.AddressOfData; names++, addrs++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (lstrcmpA((const char *)ibn->Name, fn) != 0) continue;
            DWORD old;
            if (VirtualProtect(&addrs->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) {
                addrs->u1.Function = (DWORD_PTR)repl;
                VirtualProtect(&addrs->u1.Function, sizeof(void *), old, &old);
            }
        }
    }
}

static void *real(const char *dll, const char *fn)
{
    return (void *)GetProcAddress(GetModuleHandleA(dll), fn);
}


static void install(void)
{
    HMODULE exe = GetModuleHandleA(NULL);

    /* DrawThemeText forwards to DrawThemeTextEx even if the exe never asks for it itself */
    HMODULE ux = LoadLibraryA("uxtheme.dll");
    if (ux) {
        if (!oDrawThemeTextEx)           oDrawThemeTextEx           = (void *)GetProcAddress(ux, "DrawThemeTextEx");
        if (!oGetThemeColor)             oGetThemeColor             = (void *)GetProcAddress(ux, "GetThemeColor");
        if (!oDrawThemeBackground)       oDrawThemeBackground       = (void *)GetProcAddress(ux, "DrawThemeBackground");
        if (!oDrawThemeText)             oDrawThemeText             = (void *)GetProcAddress(ux, "DrawThemeText");
        if (!oDrawThemeParentBackground) oDrawThemeParentBackground = (void *)GetProcAddress(ux, "DrawThemeParentBackground");
        if (!oDrawThemeEdge)             oDrawThemeEdge             = (void *)GetProcAddress(ux, "DrawThemeEdge");
    }

    oSetTextColor        = real("gdi32.dll", "SetTextColor");
    oSetBkColor          = real("gdi32.dll", "SetBkColor");
    oCreateSolidBrush    = real("gdi32.dll", "CreateSolidBrush");
    oCreateBrushIndirect = real("gdi32.dll", "CreateBrushIndirect");
    oCreatePenIndirect   = real("gdi32.dll", "CreatePenIndirect");
    oExtCreatePen        = real("gdi32.dll", "ExtCreatePen");
    oGetStockObject      = real("gdi32.dll", "GetStockObject");
    oPatBlt              = real("gdi32.dll", "PatBlt");
    oGetSysColorBrush    = real("user32.dll", "GetSysColorBrush");
    oGetSysColor         = real("user32.dll", "GetSysColor");
    oSendMessageW        = real("user32.dll", "SendMessageW");
    oDrawEdge            = real("user32.dll", "DrawEdge");
    oDrawFrameControl    = real("user32.dll", "DrawFrameControl");
    oEndPaint            = real("user32.dll", "EndPaint");
    oGetProcAddress      = (FARPROC(WINAPI *)(HMODULE, LPCSTR))real("kernel32.dll", "GetProcAddress");

    /* OpenIV.exe: VCL drawing and run-time bound uxtheme / msimg32 */
    patch_iat(exe, "kernel32.dll", "GetProcAddress", hGetProcAddress);
    patch_iat(exe, "user32.dll", "SendMessageW", hSendMessageW);
    patch_iat(exe, "user32.dll", "DrawEdge", hDrawEdge);
    patch_iat(exe, "user32.dll", "DrawFrameControl", hDrawFrameControl);
    patch_iat(exe, "user32.dll", "EndPaint", hEndPaint);
    patch_iat(exe, "user32.dll", "GetSysColorBrush", hGetSysColorBrush);
    patch_iat(exe, "gdi32.dll", "GetStockObject", hGetStockObject);
    patch_iat(exe, "gdi32.dll", "PatBlt", hPatBlt);
    patch_iat(exe, "gdi32.dll", "SetTextColor", hSetTextColor);
    patch_iat(exe, "gdi32.dll", "SetBkColor", hSetBkColor);
    patch_iat(exe, "gdi32.dll", "CreateSolidBrush", hCreateSolidBrush);
    patch_iat(exe, "gdi32.dll", "CreateBrushIndirect", hCreateBrushIndirect);
    patch_iat(exe, "gdi32.dll", "CreatePenIndirect", hCreatePenIndirect);
    patch_iat(exe, "gdi32.dll", "ExtCreatePen", hExtCreatePen);

    /* The native common controls (header, toolbar, ...) read system colours and draw themes themselves. */
    HMODULE cc = GetModuleHandleA("comctl32.dll");
    if (cc) {
        patch_iat(cc, "user32.dll", "GetSysColor", hGetSysColor);
        patch_iat(cc, "user32.dll", "GetSysColorBrush", hGetSysColorBrush);
        patch_iat(cc, "user32.dll", "DrawEdge", hDrawEdge);
        patch_iat(cc, "user32.dll", "DrawFrameControl", hDrawFrameControl);
        patch_iat(cc, "user32.dll", "EndPaint", hEndPaint);
        patch_delay_iat(cc, "uxtheme.dll", "DrawThemeBackground", hDrawThemeBackground);
        patch_delay_iat(cc, "uxtheme.dll", "DrawThemeText", hDrawThemeText);
        patch_delay_iat(cc, "uxtheme.dll", "DrawThemeTextEx", hDrawThemeTextEx);
        patch_delay_iat(cc, "uxtheme.dll", "GetThemeColor", hGetThemeColor);
        patch_delay_iat(cc, "uxtheme.dll", "DrawThemeParentBackground", hDrawThemeParentBackground);
    }

    HMODULE du = LoadLibraryA("DUser.dll");
    if (du) {
        patch_iat(du, "user32.dll", "EndPaint", hEndPaint);
    }

    oCreateWindowExW = real("user32.dll", "CreateWindowExW");
    patch_iat(exe, "user32.dll", "CreateWindowExW", hCreateWindowExW);

    oMessageBoxW = real("user32.dll", "MessageBoxW");
    oMessageBoxA = real("user32.dll", "MessageBoxA");
    patch_iat(exe, "user32.dll", "MessageBoxW", hMessageBoxW);
    patch_iat(exe, "user32.dll", "MessageBoxA", hMessageBoxA);
}

/* ---------- dinput8 proxy ---------- */

typedef HRESULT(WINAPI *DI8Create)(HINSTANCE, DWORD, const GUID *, LPVOID *, LPUNKNOWN);

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, const GUID *riid, LPVOID *out, LPUNKNOWN outer)
{
    static DI8Create fn;
    if (!fn) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH); /* SysWOW64 for a 32-bit process */
        lstrcatA(path, "\\dinput8.dll");
        HMODULE m = LoadLibraryA(path);
        if (m) fn = (DI8Create)GetProcAddress(m, "DirectInput8Create");
    }
    return fn ? fn(inst, ver, riid, out, outer) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        install();
    }
    return TRUE;
}
