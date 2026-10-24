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

/* Style each list / tree view right after the VCL created its window (also after every re-creation). */
static HWND (WINAPI *oCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);

static HWND WINAPI hCreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h,
                                    HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    HWND wnd = oCreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (wnd && HIWORD(cls) != 0) { /* a string class name, not an atom */
        dark_init_once();
        style_window(wnd);
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

static HRESULT WINAPI hDrawThemeBackground(HTHEME_ th, HDC dc, int part, int state, const RECT *r, const RECT *clip)
{
    Snap s;
    int ok = r && snap_begin(dc, r, clip, &s);
    HRESULT hr = oDrawThemeBackground(th, dc, part, state, r, clip);
    if (ok) snap_end(dc, &s);
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

    oCreateWindowExW = real("user32.dll", "CreateWindowExW");
    patch_iat(exe, "user32.dll", "CreateWindowExW", hCreateWindowExW);
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
