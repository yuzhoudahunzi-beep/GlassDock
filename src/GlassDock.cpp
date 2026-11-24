#define NOMINMAX
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cwchar>
#include <cstdlib>
#include <cmath>

#include "crisptext.h"

using namespace Gdiplus;

// ---------------- globals ----------------
static HINSTANCE g_hInst = NULL;
static HWND g_hwnd = NULL;
static ULONG_PTR g_gdiplusToken = 0;
static float g_scale = 1.0f;
static DWORD g_liveBgInterval = 16;   // ms between live backdrop rebuilds (from refreshHz)

// The PARGB bitmap that is currently being painted.  The crisp text renderer
// writes the glyph coverage straight into it (see crisptext.h), so it has to
// know which surface the active Graphics is bound to.
static Bitmap* g_paintDst = NULL;

static int S(int v){ return (int)(v * g_scale + 0.5f); }

// ---------------- config ----------------
struct Config {
    std::wstring folder;
    int width = 420, height = 560;
    int x = -1, y = -1;
    int opacity = 36;              // 0..100  tint strength (100 = solid color)
    COLORREF accent = RGB(128, 128, 128);   // neutral grey: pure glass, no colour cast
    bool topmost = true;           // sticky by default so the widget is never lost behind windows
    bool autostart = false;
    bool foldersFirst = true;
    bool showExt = true;
    int refreshHz = 60;            // live drag/resize refresh rate (30..240 Hz)
};
static Config cfg;
static std::wstring g_cfgFile;

// ---------------- small helpers ----------------
static float ColorLum(COLORREF c){
    return 0.299f * GetRValue(c) / 255.0f + 0.587f * GetGValue(c) / 255.0f + 0.114f * GetBValue(c) / 255.0f;
}

static int ciCompare(const std::wstring& a, const std::wstring& b){
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++){
        wchar_t ca = towlower(a[i]), cb = towlower(b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

static std::wstring AppDataDir(){
    wchar_t buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH) == 0) return L".";
    return std::wstring(buf) + L"\\GlassDock";
}

static int GetIniInt(const wchar_t* key, int def){
    wchar_t buf[64];
    GetPrivateProfileStringW(L"GlassDock", key, L"", buf, 64, g_cfgFile.c_str());
    if (buf[0]) return (int)wcstol(buf, NULL, 10);
    return def;
}

static std::wstring GetIniStr(const wchar_t* key, const wchar_t* def){
    wchar_t buf[MAX_PATH * 2];
    GetPrivateProfileStringW(L"GlassDock", key, def, buf, MAX_PATH * 2, g_cfgFile.c_str());
    return buf;
}

static void SetIniInt(const wchar_t* key, int v){
    wchar_t buf[32];
    wsprintfW(buf, L"%d", v);
    WritePrivateProfileStringW(L"GlassDock", key, buf, g_cfgFile.c_str());
}

static void SetIniStr(const wchar_t* key, const std::wstring& v){
    WritePrivateProfileStringW(L"GlassDock", key, v.c_str(), g_cfgFile.c_str());
}

static COLORREF ParseHex(const std::wstring& s, COLORREF def){
    if (s.size() >= 6){
        unsigned long v = wcstoul(s.c_str(), NULL, 16);
        return RGB((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    }
    return def;
}

static std::wstring HexColor(COLORREF c){
    wchar_t buf[16];
    wsprintfW(buf, L"%06X", (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c));
    return buf;
}

static void LoadConfig(){
    CreateDirectoryW(AppDataDir().c_str(), NULL);
    g_cfgFile = AppDataDir() + L"\\config.ini";

    cfg.folder = GetIniStr(L"folder", L"");
    if (cfg.folder.empty()){
        wchar_t buf[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, buf) == S_OK) cfg.folder = buf;
        else cfg.folder = L"C:\\";
    }
    cfg.width   = GetIniInt(L"width", 420);
    cfg.height  = GetIniInt(L"height", 560);
    cfg.x       = GetIniInt(L"x", -1);
    cfg.y       = GetIniInt(L"y", -1);
    cfg.opacity = GetIniInt(L"opacity", 36);
    cfg.accent  = ParseHex(GetIniStr(L"accent", L"808080"), RGB(128, 128, 128));
    cfg.topmost = false;          // desktop gadget: never floats above other apps
    cfg.autostart = GetIniInt(L"autostart", 0) != 0;
    cfg.foldersFirst = GetIniInt(L"foldersFirst", 1) != 0;
    cfg.showExt = GetIniInt(L"showExt", 1) != 0;
    cfg.refreshHz = GetIniInt(L"refreshHz", 60);

    if (cfg.width  < 200) cfg.width  = 200;
    if (cfg.height < 160) cfg.height = 160;
    if (cfg.width  > 1200) cfg.width  = 1200;
    if (cfg.height > 1600) cfg.height = 1600;
    if (cfg.opacity < 0) cfg.opacity = 0;
    if (cfg.opacity > 100) cfg.opacity = 100;
    if (cfg.refreshHz < 30) cfg.refreshHz = 30;
    if (cfg.refreshHz > 240) cfg.refreshHz = 240;
}

static void SavePos();

static void SaveConfig(){
    SetIniStr(L"folder", cfg.folder);
    SetIniInt(L"width", cfg.width);
    SetIniInt(L"height", cfg.height);
    SetIniInt(L"opacity", cfg.opacity);
    SetIniStr(L"accent", HexColor(cfg.accent));
    SetIniInt(L"topmost", 0);      // kept for backwards compatibility, always 0 now
    SetIniInt(L"autostart", cfg.autostart ? 1 : 0);
    SetIniInt(L"foldersFirst", cfg.foldersFirst ? 1 : 0);
    SetIniInt(L"showExt", cfg.showExt ? 1 : 0);
    SetIniInt(L"refreshHz", cfg.refreshHz);
    SavePos();
}

static void SavePos(){
    if (!g_hwnd) return;
    RECT r; GetWindowRect(g_hwnd, &r);
    SetIniInt(L"x", r.left);
    SetIniInt(L"y", r.top);
}

static void ApplyAutoStart(){
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) == ERROR_SUCCESS){
        if (cfg.autostart){
            wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
            std::wstring v = L"\"" + std::wstring(exe) + L"\"";
            RegSetValueExW(k, L"GlassDock", 0, REG_SZ, (const BYTE*)v.c_str(),
                           (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(k, L"GlassDock");
        }
        RegCloseKey(k);
    }
}

// ---------------- fonts ----------------
// GDI+ rasterises small CJK glyphs with its own soft grayscale anti-aliaser.
// At 12-15 px a stroke is thinner than one device pixel, so GDI+ splits it over
// two half-covered grey pixels.  Measured on the widget's own list text that
// left only ~7 % of the stroke pixels solid white while the rest were muddy
// grey  ->  that is precisely what "发虚" looks like.
//
// Windows draws its own UI text with GDI (grayscale AA, hinted) and Explorer
// draws the desktop icon labels with GDI ClearType.  We therefore keep a GDI
// twin for every GDI+ font and render the text through GDI instead: the solid
// share goes up to ~55 % and the edge transition drops from 1.98 px to 1.08 px,
// i.e. the label reads exactly like a native Windows label.
// See src/crisptext.h for the details.
static Font* g_fontUi = NULL;
static Font* g_fontBold = NULL;
static Font* g_fontSmall = NULL;
static Font* g_fontGlyph = NULL;
static Font* g_fontGlyphSmall = NULL;
static Font* g_fontWatermark = NULL;

// the GDI twins
static HFONT g_hfUi = NULL;
static HFONT g_hfBold = NULL;
static HFONT g_hfSmall = NULL;
static HFONT g_hfGlyph = NULL;
static HFONT g_hfGlyphSmall = NULL;
static HFONT g_hfWatermark = NULL;

// map a GDI+ font onto its GDI twin
static HFONT HFontFor(Font* f){
    if (f == g_fontUi)         return g_hfUi;
    if (f == g_fontBold)       return g_hfBold;
    if (f == g_fontSmall)      return g_hfSmall;
    if (f == g_fontGlyph)      return g_hfGlyph;
    if (f == g_fontGlyphSmall) return g_hfGlyphSmall;
    if (f == g_fontWatermark)  return g_hfWatermark;
    return NULL;
}

static void InitFonts(){
    NONCLIENTMETRICSW ncm = {0};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    float px = (float)(lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight);
    if (px < 8) px = 12;

    FontFamily fam(lf.lfFaceName);
    bool useSeg = (fam.GetLastStatus() != Ok);
    const wchar_t* face = useSeg ? L"Segoe UI" : lf.lfFaceName;
    if (useSeg){
        g_fontUi   = new Font(L"Segoe UI", px, FontStyleRegular, UnitPixel);
        g_fontBold = new Font(L"Segoe UI", px, FontStyleBold, UnitPixel);
        g_fontSmall = new Font(L"Segoe UI", px - 1.5f, FontStyleRegular, UnitPixel);
    } else {
        g_fontUi   = new Font(&fam, px, FontStyleRegular, UnitPixel);
        g_fontBold = new Font(&fam, px, FontStyleBold, UnitPixel);
        g_fontSmall = new Font(&fam, px - 1.5f, FontStyleRegular, UnitPixel);
    }
    // GDI twins – same face, same pixel height, hinted grayscale AA
    g_hfUi    = crisp::MakeHFont(face, (int)(px + 0.5f), false);
    g_hfBold  = crisp::MakeHFont(face, (int)(px + 0.5f), true);
    g_hfSmall = crisp::MakeHFont(face, (int)(px - 1.5f + 0.5f), false);

    const wchar_t* glyphFace = L"Segoe Fluent Icons";
    {
        FontFamily gf(glyphFace);
        if (gf.GetLastStatus() != Ok) glyphFace = L"Segoe MDL2 Assets";
    }
    g_fontGlyph      = new Font(glyphFace, (REAL)S(13), FontStyleRegular, UnitPixel);
    g_fontGlyphSmall = new Font(glyphFace, (REAL)S(11), FontStyleRegular, UnitPixel);
    g_fontWatermark  = new Font(glyphFace, (REAL)S(60), FontStyleRegular, UnitPixel);
    g_hfGlyph        = crisp::MakeHFont(glyphFace, S(13), false);
    g_hfGlyphSmall   = crisp::MakeHFont(glyphFace, S(11), false);
    g_hfWatermark    = crisp::MakeHFont(glyphFace, S(60), false);
}

// ---------------- background capture / frosted glass ----------------
// The see-through colour-key. Pixels painted with this colour are made fully
// transparent by the layered-window colour key, so the live desktop (including
// Wallpaper Engine's animated wallpaper) shows through. We pick a garish magenta
// that never occurs in real UI content.
#define TRANSPARENT_KEY RGB(255, 0, 255)

static Bitmap* g_bg = NULL;          // processed background for the widget
static Bitmap* g_screenShot = NULL;  // full desktop snapshot (taken with our windows hidden)
static int g_vx = 0, g_vy = 0, g_vw = 1, g_vh = 1;
static float g_desktopLum = 0.5f;

// settings panel state
static HWND g_setHwnd = NULL;

static void RoundCorners(HWND h){
    // Disable DWM's own corner rounding. These are per-pixel-alpha (UpdateLayeredWindow)
    // windows, so their corners are defined entirely by the alpha we paint — a second
    // DWM clip at a different radius (Win11's fixed ~8px) would fight our S(12) rounded
    // corners and produce the visible corner "misalignment". Self-drawn corners only.
    int pref = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof(pref));
}

// ---------------- screen capture helpers ----------------
// Used only by the legacy wallpaper-snapshot fallback path (RefreshDesktopSnapshot's
// forced re-grab). The main widget and the settings panel are BOTH true see-through
// layered windows now, so this is not on their hot path.

static Bitmap* CaptureRegion(int sx, int sy, int w, int h){
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, sx, sy, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    Bitmap* b = Bitmap::FromHBITMAP(bmp, NULL);
    DeleteObject(bmp);
    return b;
}

static Bitmap* BlurBitmap(Bitmap* src, int factor){
    int w = src->GetWidth(), h = src->GetHeight();
    if (w <= 0 || h <= 0) return NULL;
    int f = factor < 1 ? 1 : factor;
    if (f == 1) return src->Clone(0, 0, w, h, PixelFormat32bppARGB);

    int sw = std::max(1, w / f), sh = std::max(1, h / f);
    // pyramid downscale with bicubic: far smoother than one bilinear jump
    Bitmap* cur = src->Clone(0, 0, w, h, PixelFormat32bppARGB);
    int cw = w, ch = h;
    while (cw > sw + 2 || ch > sh + 2){
        int nw = std::max(sw, cw / 2), nh = std::max(sh, ch / 2);
        if (nw == cw && nh == ch) break;
        Bitmap* nxt = new Bitmap(nw, nh, PixelFormat32bppARGB);
        Graphics gn(nxt);
        gn.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        gn.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        gn.DrawImage(cur, 0, 0, nw, nh);
        delete cur;
        cur = nxt; cw = nw; ch = nh;
    }
    if (cw != sw || ch != sh){
        Bitmap* nxt = new Bitmap(sw, sh, PixelFormat32bppARGB);
        Graphics gn(nxt);
        gn.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        gn.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        gn.DrawImage(cur, 0, 0, sw, sh);
        delete cur;
        cur = nxt; cw = sw; ch = sh;
    }

    Bitmap* out = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics go(out);
    go.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    go.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    go.DrawImage(cur, 0, 0, w, h);
    delete cur;
    return out;
}

static float ComputeLuminance(Bitmap* b){
    int w = b->GetWidth(), h = b->GetHeight();
    if (w <= 0 || h <= 0) return 0.5f;
    long sum = 0; int cnt = 0;
    int step = std::max(1, std::min(w, h) / 24);
    for (int y = 0; y < h; y += step){
        for (int x = 0; x < w; x += step){
            Color c; b->GetPixel(x, y, &c);
            sum += (c.GetR() * 299 + c.GetG() * 587 + c.GetB() * 114) / 1000;
            cnt++;
        }
    }
    if (!cnt) return 0.5f;
    return (sum / (float)cnt) / 255.0f;
}

// ---------------- background: what is really behind the widget ----------------
// The widget lives on the desktop, so what lies behind it is the wallpaper (plus
// desktop icons). Screen-grabbing would only ever reproduce our own window, and
// PrintWindow on the shell often fails, so we rebuild the wallpaper layer from the
// file Windows itself uses. Dynamic wallpapers are still picked up when the shell
// does expose a readable surface.
static Bitmap* g_wallImage = NULL;      // cached source image
static std::wstring g_wallPath;         // path it was loaded from
static ULARGE_INTEGER g_wallStamp = {{0}};

static std::wstring WallpaperPath(){
    wchar_t buf[1024] = {0};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper",
                     RRF_RT_REG_SZ, NULL, buf, &sz) != ERROR_SUCCESS) return L"";
    std::wstring p = buf;
    wchar_t ex[MAX_PATH * 2] = {0};
    if (ExpandEnvironmentStringsW(p.c_str(), ex, MAX_PATH * 2)) p = ex;
    return p;
}

static bool WallpaperStamp(const std::wstring& path, ULARGE_INTEGER* out){
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    out->LowPart = fad.nFileSizeLow; out->HighPart = fad.nFileSizeHigh;
    return true;
}

static void FreeWallpaperCache(){
    if (g_wallImage){ delete g_wallImage; g_wallImage = NULL; }
    g_wallPath.clear();
    g_wallStamp.QuadPart = 0;
}

// cover / fit / stretch / center, as Windows lays the wallpaper out on the primary monitor
static int WallpaperStyle(){
    wchar_t style[16] = {0}, tile[8] = {0};
    DWORD sz = sizeof(style);
    RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallpaperStyle",
                 RRF_RT_REG_SZ, NULL, style, &sz);
    sz = sizeof(tile);
    RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"TileWallpaper",
                 RRF_RT_REG_SZ, NULL, tile, &sz);
    int s = _wtoi(style);
    if (_wtoi(tile) == 1 && s == 0) return 1;   // tiled
    return s;                                    // 10 fill, 6 fit, 2 stretch, 0 center
}

// render the wallpaper the way the desktop shows it, cropped to the widget rect
static Bitmap* RenderWallpaperLayer(){
    std::wstring path = WallpaperPath();
    if (path.empty()) return NULL;
    ULARGE_INTEGER stamp = {{0}};
    if (!WallpaperStamp(path, &stamp)) return NULL;

    if (!g_wallImage || g_wallPath != path || g_wallStamp.QuadPart != stamp.QuadPart){
        FreeWallpaperCache();
        g_wallImage = new Bitmap(path.c_str());
        if (!g_wallImage || g_wallImage->GetLastStatus() != Ok){
            FreeWallpaperCache();
            return NULL;
        }
        g_wallPath = path;
        g_wallStamp = stamp;
    }

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return NULL;

    Bitmap* out = new Bitmap(vw, vh, PixelFormat32bppARGB);
    Graphics g(out);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    int mw = GetSystemMetrics(SM_CXSCREEN), mh = GetSystemMetrics(SM_CYSCREEN);
    if (mw <= 0 || mh <= 0){ mw = vw; mh = vh; }
    int iw = (int)g_wallImage->GetWidth(), ih = (int)g_wallImage->GetHeight();
    int style = WallpaperStyle();

    double dw, dh;
    if (style == 2){ dw = mw; dh = mh; }                       // stretch
    else if (style == 6){                                       // fit
        double s = std::min((double)mw / iw, (double)mh / ih);
        dw = iw * s; dh = ih * s;
    } else if (style == 0){                                      // center (native size)
        dw = iw; dh = ih;
    } else {                                                    // fill / span / fallback
        double s = std::max((double)mw / iw, (double)mh / ih);
        dw = iw * s; dh = ih * s;
    }
    double ox = -vx + (mw - dw) / 2.0, oy = -vy + (mh - dh) / 2.0;
    RectF dst((REAL)ox, (REAL)oy, (REAL)dw, (REAL)dh);
    g.DrawImage(g_wallImage, dst);

    // secondary monitors: keep extending the same picture so nothing is left black
    if (vw > mw || vh > mh){
        g.DrawImage(g_wallImage, RectF((REAL)(ox + dw), (REAL)oy, (REAL)dw, (REAL)dh));
    }
    return out;
}

static void HideOwnWindows(BOOL* mainVis, BOOL* setVis){
    *mainVis = (g_hwnd && IsWindowVisible(g_hwnd));
    *setVis  = (g_setHwnd && IsWindowVisible(g_setHwnd));
    if (*mainVis) ShowWindow(g_hwnd, SW_HIDE);
    if (*setVis)  ShowWindow(g_setHwnd, SW_HIDE);
    DwmFlush();
}
static void RestoreOwnWindows(BOOL mainVis, BOOL setVis){
    if (mainVis && g_hwnd) ShowWindow(g_hwnd, SW_SHOWNA);
    if (setVis && g_setHwnd) ShowWindow(g_setHwnd, SW_SHOWNA);
    DwmFlush();
}

static int  g_bgSource = 0;              // 0 none, 2 wallpaper file, 3 screen grab

// refresh the cached backdrop; returns true when the image actually changed.
// We render the wallpaper *file* directly (correct on Win10/11 where PrintWindow can't
// see the DWM-composited wallpaper and would otherwise capture our own widget into the
// snapshot — the source of the "darkens frame by frame" bug). Rebuild only when the
// picture or the monitor layout changed, so a static wallpaper is rendered once.
static bool RefreshDesktopSnapshot(bool force = false){
    if (!g_hwnd) return false;

    ULARGE_INTEGER stamp = {{0}};
    std::wstring path = WallpaperPath();
    bool sameWall = (g_bgSource == 2 && path == g_wallPath &&
                     WallpaperStamp(path, &stamp) && stamp.QuadPart == g_wallStamp.QuadPart);
    if (sameWall && !force) return false;

    Bitmap* wall = RenderWallpaperLayer();
    if (wall){
        delete g_screenShot; g_screenShot = wall;
        g_bgSource = 2;
        g_vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        g_vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        g_vw = wall->GetWidth(); g_vh = wall->GetHeight();
        return true;
    }

    // fallback: briefly hide ourselves and grab the screen (only when forced)
    if (force){
        RECT wr; GetWindowRect(g_hwnd, &wr);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;
        BOOL mv, sv; HideOwnWindows(&mv, &sv);
        Bitmap* grab = CaptureRegion(wr.left, wr.top, w, h);
        RestoreOwnWindows(mv, sv);
        if (grab){
            delete g_screenShot; g_screenShot = grab;
            g_bgSource = 3;
            g_vx = wr.left; g_vy = wr.top;
            g_vw = w; g_vh = h;
            return true;
        }
    }
    return false;
}

// is the widget actually visible on the desktop, or covered by some app?
static bool WidgetObscured(){
    if (!g_hwnd || !IsWindowVisible(g_hwnd)) return true;
    if (IsIconic(g_hwnd)) return true;
    RECT wr; GetWindowRect(g_hwnd, &wr);
    POINT pts[3];
    pts[0] = {wr.left + 8, wr.top + 8};
    pts[1] = {(wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2};
    pts[2] = {wr.right - 8, wr.bottom - 8};
    for (int i = 0; i < 3; i++){
        HWND h = WindowFromPoint(pts[i]);
        if (!h || h == g_hwnd) continue;
        wchar_t cls[64] = {0};
        GetClassNameW(h, cls, 64);
        if (lstrcmpW(cls, L"Progman") == 0 || lstrcmpW(cls, L"WorkerW") == 0 ||
            lstrcmpW(cls, L"SysListView32") == 0 || lstrcmpW(cls, L"SHELLDLL_DefView") == 0)
            continue;                       // desktop itself, not an app
        return true;
    }
    return false;
}

// a downscaled copy of the cached backdrop, kept so live drag can re-crop from a
// small source instead of the full-resolution snapshot every frame. This is what
// makes the background track the pointer smoothly instead of stuttering.
static Bitmap* g_screenShotSmall = NULL;
static int g_ssSmallScale = 0;
static Bitmap* g_ssSmallSrc = NULL;   // which snapshot the small copy was built from

static Bitmap* CropDesktop(int wx, int wy, int w, int h, bool fast = false){
    if (!g_screenShot || w <= 0 || h <= 0) return NULL;

    Bitmap* src = g_screenShot;
    int sx = g_vx, sy = g_vy, sw = w, sh = h;
    int x0 = wx - g_vx, y0 = wy - g_vy;

    if (fast){
        // build (once) a small copy of the full snapshot so re-cropping is cheap
        int imgW = g_screenShot->GetWidth(), imgH = g_screenShot->GetHeight();
        int scale = std::max(1, std::min(imgW, imgH) / 512);
        if (!g_screenShotSmall || g_ssSmallScale != scale || g_ssSmallSrc != g_screenShot){
            if (g_screenShotSmall) delete g_screenShotSmall;
            g_screenShotSmall = new Bitmap(std::max(1, imgW / scale), std::max(1, imgH / scale), PixelFormat32bppARGB);
            Graphics gs(g_screenShotSmall);
            gs.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            gs.DrawImage(g_screenShot, 0, 0, g_screenShotSmall->GetWidth(), g_screenShotSmall->GetHeight());
            g_ssSmallScale = scale;
            g_ssSmallSrc = g_screenShot;
        }
        src = g_screenShotSmall;
        sx = g_vx / scale; sy = g_vy / scale;
        x0 = (wx - g_vx) / scale; y0 = (wy - g_vy) / scale;
        sw = w / scale; sh = h / scale;
    }

    int imgW = src->GetWidth(), imgH = src->GetHeight();
    if (x0 < 0){ sw += x0; x0 = 0; }
    if (y0 < 0){ sh += y0; y0 = 0; }
    if (x0 + sw > imgW) sw = imgW - x0;
    if (y0 + sh > imgH) sh = imgH - y0;
    if (sw <= 0 || sh <= 0) return NULL;

    Bitmap* out = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(out);
    // fast path uses bilinear (cheap); full path keeps high quality
    g.SetInterpolationMode(fast ? InterpolationModeBilinear : InterpolationModeHighQualityBilinear);
    g.SetPixelOffsetMode(fast ? PixelOffsetModeHalf : PixelOffsetModeHighQuality);
    g.DrawImage(src, RectF(0, 0, (REAL)w, (REAL)h), x0, y0, sw, sh, UnitPixel);
    return out;
}

// build a blurred + tinted image of the requested size from a raw crop.
// `maxFactor` clamps the blur pyramid depth; live drag uses a small value so the
// backdrop tracks the pointer in real time (the wallpaper is already cached, so
// re-cropping is just a cheap in-memory resize).
// `fast` skips the saturation pass and the legibility veil entirely and reuses the
// last known luminance, so a live drag only pays for crop + shallow blur + tint.
static Bitmap* MakeBg(Bitmap* raw, int w, int h, float* lumOut, int maxFactor = 24, bool fast = false){
    if (!raw || w <= 0 || h <= 0) return NULL;
    int factor = 1;                 // blur slider removed — backdrop is no longer blurred
    if (factor > maxFactor) factor = maxFactor;
    Bitmap* blurred = BlurBitmap(raw, factor);
    float lum = (fast && lumOut && *lumOut >= 0.0f) ? *lumOut : ComputeLuminance(blurred);
    if (lumOut) *lumOut = lum;

    Bitmap* out = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(out);
    if (fast){
        g.SetInterpolationMode(InterpolationModeBilinear);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(blurred, 0, 0, w, h);
    } else {
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        // draw the blurred wallpaper as-is: no saturation lift, so a neutral grey
        // accent stays perfectly neutral and the glass shows the true wallpaper colour
        g.DrawImage(blurred, 0, 0, w, h);
    }
    delete blurred;

    // The opacity slider is now a direct, linear glass-opacity control: 0 = the
    // wallpaper shows through untouched (pure see-through), 100 = a fully solid
    // panel of the accent colour. No gamma curve, no hidden clamp — what you set is
    // exactly what you get. This is what finally makes "transparent" mean transparent
    // and removes the faint blue/grey cast users kept seeing.
    float o = cfg.opacity / 100.0f;
    int a = (int)(o * 255.0f + 0.5f);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    if (a > 0){
        SolidBrush br(Color((BYTE)a, GetRValue(cfg.accent), GetGValue(cfg.accent), GetBValue(cfg.accent)));
        g.FillRectangle(&br, 0, 0, w, h);
    }

    if (!fast){
        // a very light veil keeps text legible on busy wallpapers without killing the glass
        float accL = ColorLum(cfg.accent);
        float w2 = o; if (w2 > 1.0f) w2 = 1.0f;
        float eff = accL * w2 + lum * (1.0f - w2);
        bool lightText = (eff <= 0.58f);
        BYTE va = (BYTE)(lightText ? 9 : 7);
        SolidBrush veil(lightText ? Color(va, 0, 0, 0) : Color(va, 255, 255, 255));
        g.FillRectangle(&veil, 0, 0, w, h);
    }
    return out;
}

static void RebuildMainBg(){
    if (!g_hwnd) return;
    RECT rc; GetClientRect(g_hwnd, &rc);
    RECT wr; GetWindowRect(g_hwnd, &wr);
    Bitmap* raw = CropDesktop(wr.left, wr.top, rc.right, rc.bottom);
    Bitmap* old = g_bg;
    g_bg = MakeBg(raw, rc.right, rc.bottom, &g_desktopLum);
    if (old) delete old;
    if (raw) delete raw;
}

// same as RebuildMainBg but with a shallower blur so it keeps up with live drag.
// Called every WM_MOUSEMOVE while dragging: the wallpaper snapshot is already
// cached in g_screenShot, so this is only a crop + light blur + tint, no screen
// capture, no file IO — it tracks the pointer like a real pane of glass.
static void RebuildMainBgLive(){
    if (!g_hwnd) return;
    RECT rc; GetClientRect(g_hwnd, &rc);
    RECT wr; GetWindowRect(g_hwnd, &wr);
    // fast crop from a downscaled snapshot + shallow blur + skip saturation/veil
    Bitmap* raw = CropDesktop(wr.left, wr.top, rc.right, rc.bottom, true);
    Bitmap* old = g_bg;
    g_bg = MakeBg(raw, rc.right, rc.bottom, &g_desktopLum, 3, true);
    if (old) delete old;
    if (raw) delete raw;
}

static void Repaint();   // forward decl: re-submit the layered surface (defined below)

static void RenderBackground(){
    if (!g_hwnd) return;
    // True see-through mode: the background is fully transparent (per-pixel alpha),
    // not a wallpaper snapshot, so there is nothing to capture. Just re-submit.
    Repaint();
}

// ---------------- items & rows ----------------
struct Item {
    std::wstring path;
    std::wstring name;
    bool isDir = false;
    bool expanded = false;
    bool childrenLoaded = false;
    std::vector<Item*> children;
    ~Item(){ for (auto c : children) delete c; }
    std::wstring DisplayName(){
        if (!cfg.showExt && !isDir){
            size_t p = name.find_last_of(L'.');
            if (p != std::wstring::npos && p > 0) return name.substr(0, p);
        }
        return name;
    }
};

struct Row { Item* item; int depth; };

static Item* g_root = NULL;
static std::vector<Row> g_rows;

static void LoadChildren(Item* dir){
    if (!dir->isDir || dir->childrenLoaded) return;
    dir->childrenLoaded = true;

    WIN32_FIND_DATAW fd;
    std::wstring pat = dir->path + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) continue;
        Item* it = new Item();
        it->path = dir->path + L"\\" + fd.cFileName;
        it->name = fd.cFileName;
        it->isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        dir->children.push_back(it);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(dir->children.begin(), dir->children.end(), [](Item* a, Item* b){
        if (cfg.foldersFirst && a->isDir != b->isDir) return a->isDir;
        return ciCompare(a->name, b->name) < 0;
    });
}

static void AddRows(Item* it, int depth){
    if (it->isDir && it->expanded){
        if (!it->childrenLoaded) LoadChildren(it);
        g_rows.push_back({it, depth});
        for (auto c : it->children) AddRows(c, depth + 1);
    } else {
        g_rows.push_back({it, depth});
    }
}

static void RebuildRows(){
    g_rows.clear();
    if (!g_root) return;
    if (!g_root->childrenLoaded) LoadChildren(g_root);
    for (auto c : g_root->children) AddRows(c, 0);
}

static void LoadRoot(){
    if (g_root) delete g_root;
    g_root = new Item();
    std::wstring f = cfg.folder;
    while (f.size() > 1 && f.back() == L'\\') f.pop_back();
    g_root->path = f;
    size_t p = f.find_last_of(L'\\');
    g_root->name = (p == std::wstring::npos) ? f : f.substr(p + 1);
    if (g_root->name.empty()) g_root->name = L"此电脑";
    g_root->isDir = true;
    g_root->expanded = true;
    LoadChildren(g_root);
    RebuildRows();
}

// ---------------- icons ----------------
static std::map<std::wstring, Bitmap*> g_iconCache;

static std::wstring Lower(std::wstring s){
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}

// Build a GDI+ Bitmap that keeps the icon's REAL transparency.
//
// Bitmap::FromHICON() is the usual shortcut, but GDI+ flattens the icon's alpha
// channel there: every masked-out pixel comes back as opaque black, which is the
// "black frame" around every file icon (the earlier SHGFI_ICON|SHGFI_SMALLICON route
// made it worse, because that variant is a legacy 16x16 DDB icon whose transparency
// lives in a 1-bit AND mask).
//
// So we take the icon apart ourselves and rebuild an ARGB bitmap by hand:
//   * hbmColor = the colour bitmap (32bpp after the GetDIBits conversion)
//   * hbmMask  = the 1-bit AND mask (a set bit means "this pixel is transparent")
// and derive the final alpha as: colour-alpha when the colour bitmap carries one
// (modern icons have a soft alpha edge), otherwise the AND mask decides.
static Bitmap* AlphaBitmapFromHIcon(HICON hIcon){
    if (!hIcon) return NULL;
    ICONINFO ii = {0};
    if (!GetIconInfo(hIcon, &ii)) return NULL;
    HBITMAP hbmColor = ii.hbmColor;
    HBITMAP hbmMask  = ii.hbmMask;
    if (!hbmColor){
        if (hbmMask) DeleteObject(hbmMask);
        return NULL;
    }
    BITMAP bm = {0};
    GetObjectW(hbmColor, sizeof(bm), &bm);
    int w = bm.bmWidth, h = bm.bmHeight;
    if (w <= 0 || h <= 0){
        DeleteObject(hbmColor);
        if (hbmMask) DeleteObject(hbmMask);
        return NULL;
    }

    // 32bpp colour pixels, top-down (B,G,R,A in memory)
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> colorBuf((size_t)w * h * 4, 0);
    HDC dc = GetDC(NULL);
    int got = GetDIBits(dc, hbmColor, 0, h, colorBuf.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);

    // 1bpp AND mask, top-down (bit set = transparent)
    int mstride = ((w + 31) / 32) * 4;
    std::vector<BYTE> maskBuf;
    if (hbmMask){
        maskBuf.assign((size_t)mstride * h, 0);
        BITMAPINFO mi = {0};
        mi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        mi.bmiHeader.biWidth = w;
        mi.bmiHeader.biHeight = -h;
        mi.bmiHeader.biPlanes = 1;
        mi.bmiHeader.biBitCount = 1;
        mi.bmiHeader.biCompression = BI_RGB;
        HDC dc2 = GetDC(NULL);
        GetDIBits(dc2, hbmMask, 0, h, maskBuf.data(), &mi, DIB_RGB_COLORS);
        ReleaseDC(NULL, dc2);
    }

    DeleteObject(hbmColor);
    if (hbmMask) DeleteObject(hbmMask);
    if (got != h) return NULL;

    // Assemble the ARGB pixels.
    //
    // Measured on this machine (tools/diag.exe): for the 32bpp system image list,
    // GetDIBits returns the colour bitmap's alpha CORRECTLY (248 fully transparent
    // pixels + 161 soft-edge pixels for a 32x32 folder icon), while the 1-bit AND
    // mask comes back all-zero — a 32bpp ARGB icon carries no mask information.
    //
    // So the rule is: if the colour bitmap has ANY real alpha, it is authoritative
    // (alpha 0 == transparent). Only for a legacy icon without alpha do we fall back
    // to the AND mask. Getting this backwards is what turned every transparent pixel
    // into opaque black — the "black frame" around every icon.
    bool hasAlpha = false;
    for (size_t i = 0; i < colorBuf.size(); i += 4){
        if (colorBuf[i + 3] != 0){ hasAlpha = true; break; }
    }

    std::vector<BYTE> argb((size_t)w * h * 4, 0);
    for (int y = 0; y < h; y++){
        const BYTE* s = colorBuf.data() + (size_t)y * w * 4;
        const BYTE* m = maskBuf.empty() ? NULL : maskBuf.data() + (size_t)y * mstride;
        BYTE* d = argb.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; x++){
            BYTE b = s[x*4+0], g = s[x*4+1], r = s[x*4+2], a = s[x*4+3];
            BYTE fa;
            if (hasAlpha){
                fa = a;                                  // ARGB icon: alpha is truth
            } else {
                bool masked = false;
                if (m) masked = (((m[x >> 3] >> (7 - (x & 7))) & 1) != 0);
                fa = masked ? 0 : 255;                  // legacy icon: AND mask decides
            }
            d[x*4+0] = r; d[x*4+1] = g; d[x*4+2] = b; d[x*4+3] = fa;
        }
    }

    // Colour bleed: a fully transparent pixel still carries the colour it had (often
    // black). When the icon is later scaled down, that colour bleeds into the semi-
    // transparent rim and shows up as a dark fringe. So push the colour of the nearest
    // opaque neighbour into every transparent pixel.
    for (int y = 0; y < h; y++){
        BYTE* row = argb.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; x++){
            if (row[x*4+3] != 0) continue;
            for (int k = 0; k < 8 && row[x*4+3] == 0; k++){
                static const int dx8[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
                static const int dy8[8] = { 0, 0, -1, 1, -1, -1,  1,  1};
                int nx = x + dx8[k], ny = y + dy8[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const BYTE* n = argb.data() + ((size_t)ny * w + nx) * 4;
                if (n[3] == 0) continue;
                row[x*4+0] = n[0]; row[x*4+1] = n[1]; row[x*4+2] = n[2];
                break;
            }
        }
    }

    Bitmap* bmp = new Bitmap(w, h, PixelFormat32bppARGB);
    if (!bmp) return NULL;
    BitmapData bd;
    Rect rc(0, 0, w, h);
    if (bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &bd) != Ok){
        delete bmp;
        return NULL;
    }
    BYTE* dst = (BYTE*)bd.Scan0;
    for (int y = 0; y < h; y++){
        memcpy(dst + (size_t)y * bd.Stride, argb.data() + (size_t)y * w * 4, (size_t)w * 4);
    }
    bmp->UnlockBits(&bd);
    return bmp;
}

// Fetch a shell icon as a 32bpp ARGB bitmap with true transparency.
// We take the index into the system image list (a 32bpp ILC_COLOR32 list) and
// materialise the icon with ImageList_GetIcon(ILD_TRANSPARENT), then rebuild the
// bitmap by hand so the alpha survives (see AlphaBitmapFromHIcon).
static Bitmap* GetShellIcon(const std::wstring& path, bool isDir){
    SHFILEINFOW sfi = {0};
    DWORD attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    // With SHGFI_SYSICONINDEX set, SHGetFileInfoW returns the system image list handle;
    // sfi.iIcon then holds the icon's index into it.
    HIMAGELIST himl = (HIMAGELIST)SHGetFileInfoW(path.c_str(), attr, &sfi, sizeof(sfi),
                                                 SHGFI_SYSICONINDEX | SHGFI_LARGEICON);
    if (!himl) return NULL;
    HICON hIcon = ImageList_GetIcon(himl, sfi.iIcon, ILD_TRANSPARENT);
    if (!hIcon) return NULL;
    Bitmap* b = AlphaBitmapFromHIcon(hIcon);
    if (!b) b = Bitmap::FromHICON(hIcon);   // last-resort fallback
    DestroyIcon(hIcon);
    return b;
}

static Bitmap* IconFor(Item* it){
    std::wstring key = it->isDir ? L"<dir>" : Lower(it->name.substr(it->name.find_last_of(L'.') == std::wstring::npos ? 0 : it->name.find_last_of(L'.')));
    auto f = g_iconCache.find(key);
    if (f != g_iconCache.end()) return f->second;
    Bitmap* b = GetShellIcon(it->path, it->isDir);
    g_iconCache[key] = b;
    return b;
}

static void FreeIcons(){
    for (auto& kv : g_iconCache) if (kv.second) delete kv.second;
    g_iconCache.clear();
}

// ---------------- layout ----------------
static int TitleH(){ return S(36); }
static int RowH(){ return S(28); }
static int LeftPad(){ return S(12); }
static int Indent(){ return S(16); }

static void GetButtons(int w, RECT out[3]){
    int s = S(22), gap = S(4), rm = S(8);
    int x = w - rm - s;                       // settings
    out[1] = {x, S(7), x + s, S(7) + s};
    x -= (s + gap);                           // refresh
    out[0] = {x, S(7), x + s, S(7) + s};
    out[2] = {0, 0, 0, 0};                    // no close: a desktop gadget must not vanish on a stray click
}

static RECT ResizeCornerRect(int w, int h){
    int s = S(18);
    return {w - s, h - s, w, h};
}

// ---------------- text color ----------------
static Color TextColorFor(float lum){
    float accL = ColorLum(cfg.accent);
    float o = cfg.opacity / 100.0f;   // linear, matches MakeBg
    float eff = accL * o + lum * (1.0f - o);
    return eff > 0.58f ? Color(255, 45, 45, 48) : Color(255, 247, 247, 250);
}
static Color TextColor(){ return TextColorFor(g_desktopLum); }
static Color AccentSolid(){
    return Color(255, GetRValue(cfg.accent), GetGValue(cfg.accent), GetBValue(cfg.accent));
}
static Color AccentHi(){
    int r = GetRValue(cfg.accent), gg = GetGValue(cfg.accent), b = GetBValue(cfg.accent);
    r = r + (255 - r) * 48 / 100;
    gg = gg + (255 - gg) * 48 / 100;
    b = b + (255 - b) * 48 / 100;
    return Color(245, r, gg, b);
}

// ---------------- crisp text ----------------
// One entry point for every piece of text in the widget.  The string is laid
// out and rasterised by GDI (grayscale AA, hinted) and the resulting coverage
// is blended into the PARGB surface as premultiplied text.
//
// This replaces Graphics::DrawString, whose GDI+ rasteriser splits a sub-pixel
// CJK stem over two half-covered grey pixels and made every label look washed
// out.  If anything goes wrong we fall back to the old GDI+ call so the widget
// never ends up without text.
static void DrawTextCrisp(Graphics& g, const wchar_t* s, Font* f,
                          const RectF& r, StringFormat* sf, const SolidBrush* br){
    if (!s || !*s || !f || !br) return;
    Color col;
    br->GetColor(&col);
    HFONT hf = HFontFor(f);
    if (!hf || !g_paintDst){
        g.DrawString(s, -1, f, r, sf, const_cast<SolidBrush*>(br));
        return;
    }
    // the layout box in whole device pixels
    int bx = (int)floorf(r.X);
    int by = (int)floorf(r.Y);
    int bw = (int)ceilf(r.X + r.Width)  - bx;
    int bh = (int)ceilf(r.Y + r.Height) - by;
    if (bw <= 0 || bh <= 0) return;
    if (bw > 8192) bw = 8192;
    if (bh > 1024) bh = 1024;

    int ha = crisp::HLeft, va = crisp::VMiddle;
    bool ell = false;
    if (sf){
        StringAlignment a  = sf->GetAlignment();
        StringAlignment la = sf->GetLineAlignment();
        ha = (a  == StringAlignmentCenter) ? crisp::HCenter
           : (a  == StringAlignmentFar)    ? crisp::HRight : crisp::HLeft;
        va = (la == StringAlignmentCenter) ? crisp::VMiddle
           : (la == StringAlignmentFar)    ? crisp::VBottom : crisp::VTop;
        ell = (sf->GetTrimming() == StringTrimmingEllipsisCharacter);
    }
    // Desktop-icon-label look: a soft 1px dark shadow under light text (and a
    // soft light shadow under dark text) so the glyph stays readable on any
    // wallpaper.  The shadow reuses the SAME coverage mask as the glyph, so it
    // can never re-darken the stroke interior the way the old 8-way outline did.
    BYTE tr = col.GetR(), tg = col.GetG(), tb = col.GetB();
    int lum = (int)(tr * 299 + tg * 587 + tb * 114) / 1000;
    int shAlpha = 85;   // ~33% — measured from the desktop icon label's own drop shadow
    if (lum >= 128){
        // light text -> dark shadow (below-right, 1px)
        crisp::DrawWithShadow(g_paintDst, hf, s, bx, by, bw, bh, ha, va, ell,
                              tr, tg, tb, 1, 1, 0, 0, 0, shAlpha);
    } else {
        // dark text -> faint light shadow
        crisp::DrawWithShadow(g_paintDst, hf, s, bx, by, bw, bh, ha, va, ell,
                              tr, tg, tb, 1, 1, 255, 255, 255, shAlpha);
    }
}

static void DrawTextCrisp(Graphics& g, const std::wstring& s, Font* f,
                          const RectF& r, StringFormat* sf, const SolidBrush* br){
    if (s.empty()) return;
    DrawTextCrisp(g, s.c_str(), f, r, sf, br);
}

static void DrawStringShadow(Graphics& g, const std::wstring& s, Font* f, const RectF& r,
                             StringFormat* sf, Color main){
    // NO outline and NO drop shadow.
    //
    // History of the three attempts, and why they all failed:
    //  1) 8-way 1px outline  -> a 2-3px wide stem (the vertical bar of a "P") was stamped
    //     by the outline eight times, so the stroke INTERIOR got painted dark — exactly
    //     the "solid black glyph" look of the GPT-Image heading.
    //  2) single 1px down-right shadow -> reads as a smeared "double image" on the panel.
    //  3) the real culprit was never the shadow at all: the surface was premultiplied
    //     TWICE, so every anti-aliased edge of every glyph was crushed to dark grey.
    //     That made thin text look grey and muddy while the bold title stayed crisp.
    //
    // With the double-premultiply gone the glyph is a clean, single alpha ramp. Any
    // additional dark pass now only ADDS a visible fringe, so we draw the text once,
    // in one colour, and let the anti-aliasing do the blending.
    SolidBrush mb(main);
    DrawTextCrisp(g, s, f, r, sf, &mb);
}

// width of a single-line string, clamped to maxW (used for the inline header chip)
static REAL MeasureTextWidth(Graphics& g, const std::wstring& s, Font* f, int maxW){
    if (s.empty()) return 0;
    // measure with the very same engine that draws the text, otherwise the chip
    // would drift away from the title
    HFONT hf = HFontFor(f);
    if (hf) return (REAL)crisp::MeasureWidth(hf, s, maxW, false);
    StringFormat sf;
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    RectF box(0, 0, (REAL)(maxW > 0 ? maxW : 10000), (REAL)S(40));
    RectF out;
    g.MeasureString(s.c_str(), -1, f, box, &sf, &out);
    return out.Width;
}

static void RoundedRect(GraphicsPath& path, RectF r, float radius){
    float d = radius * 2.0f;
    if (d > r.Width) d = r.Width;
    if (d > r.Height) d = r.Height;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

// ---------------- paint ----------------
static int g_scroll = 0;
static int g_hoverRow = -1;
static int g_hoverBtn = -1;

static void ClampScroll(int w, int h){
    int total = (int)g_rows.size() * RowH();
    int vis = h - TitleH();
    int maxS = total - vis;
    if (maxS < 0) maxS = 0;
    if (g_scroll > maxS) g_scroll = maxS;
    if (g_scroll < 0) g_scroll = 0;
}

static void DrawTitle(Graphics& g, int w){
    int th = TitleH();
    Color tc = TextColor();
    Color dim(150, tc.GetR(), tc.GetG(), tc.GetB());

    // small folder glyph
    RectF fr((REAL)LeftPad(), 0, (REAL)S(20), (REAL)th);
    StringFormat fs;
    fs.SetAlignment(StringAlignmentCenter);
    fs.SetLineAlignment(StringAlignmentCenter);
    SolidBrush fb(Color(200, AccentHi().GetR(), AccentHi().GetG(), AccentHi().GetB()));
    DrawTextCrisp(g, L"\uE8B7", g_fontGlyphSmall, fr, &fs, &fb);

    // folder name
    RectF r((REAL)(LeftPad() + S(20)), 0, (REAL)(w - S(140)), (REAL)th);
    StringFormat sf;
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetLineAlignment(StringAlignmentCenter);
    std::wstring title = g_root ? g_root->name : L"";
    DrawStringShadow(g, title, g_fontBold, r, &sf, tc);

    // item-count chip right after the name (gives the header a real product feel)
    if (!g_rows.empty()){
        int tw = (int)MeasureTextWidth(g, title, g_fontBold, (int)r.Width);
        int cx = r.X + tw + S(8);
        if (cx + S(44) < w - S(120)){
            RECT chip = {cx, S(10), cx + S(40), S(10) + S(17)};
            RectF cr((REAL)chip.left, (REAL)chip.top, (REAL)(chip.right - chip.left), (REAL)(chip.bottom - chip.top));
            GraphicsPath cp;
            RoundedRect(cp, cr, (REAL)S(8));
            SolidBrush cb(Color(30, 255, 255, 255));
            g.FillPath(&cb, &cp);
            wchar_t cnt[24];
            wsprintfW(cnt, L"%d 项", (int)g_rows.size());
            StringFormat cf;
            cf.SetAlignment(StringAlignmentCenter);
            cf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush ctb(Color(190, tc.GetR(), tc.GetG(), tc.GetB()));
            DrawTextCrisp(g, cnt, g_fontSmall, cr, &cf, &ctb);
        }
    }

    // hairline divider under the header: accent-tinted, fading out at both ends
    {
        Color ac = AccentHi();
        RectF band((REAL)S(10), (REAL)(th - 1), (REAL)(w - S(20)), 1.0f);
        LinearGradientBrush lg(band,
                               Color(0, ac.GetR(), ac.GetG(), ac.GetB()),
                               Color(96, ac.GetR(), ac.GetG(), ac.GetB()),
                               LinearGradientModeHorizontal);
        lg.SetBlendTriangularShape(0.5f, 1.0f);
        g.FillRectangle(&lg, band);
        // faint white base keeps it visible even when the accent is very dark
        Pen dp(Color(22, 255, 255, 255), 1.0f);
        g.DrawLine(&dp, (REAL)S(10), (REAL)(th - 1), (REAL)(w - S(10)), (REAL)(th - 1));
    }

    RECT btns[3];
    GetButtons(w, btns);
    const wchar_t* glyphs[2] = { L"\uE72C", L"\uE713" }; // refresh, settings
    for (int i = 0; i < 2; i++){
        RECT b = btns[i];
        RectF br((REAL)b.left, (REAL)b.top, (REAL)(b.right - b.left), (REAL)(b.bottom - b.top));
        if (g_hoverBtn == i){
            GraphicsPath hp; RoundedRect(hp, RectF(br.X - 2, br.Y - 2, br.Width + 4, br.Height + 4), (REAL)S(7));
            Color ac = AccentHi();
            SolidBrush hb(Color(58, ac.GetR(), ac.GetG(), ac.GetB()));
            g.FillPath(&hb, &hp);
            Pen hpen(Color(95, ac.GetR(), ac.GetG(), ac.GetB()), 1.0f);
            g.DrawPath(&hpen, &hp);
        }
        StringFormat csf;
        csf.SetAlignment(StringAlignmentCenter);
        csf.SetLineAlignment(StringAlignmentCenter);
        Color ic = tc;
        if (i == 0 && g_hoverBtn == 0) ic = AccentHi();
        else if (i == 1 && g_hoverBtn == 1) ic = AccentHi();
        if (i == 2 && g_hoverBtn == 2) ic = Color(255, 255, 140, 140);
        SolidBrush tb(ic);
        DrawTextCrisp(g, glyphs[i], g_fontGlyph, br, &csf, &tb);
    }
}

static void DrawList(Graphics& g, int w, int h){
    int th = TitleH();
    int rh = RowH();
    Color tc = TextColor();

    // clip to body
    RectF body((REAL)0, (REAL)th, (REAL)w, (REAL)(h - th));
    g.SetClip(body);

    if (g_rows.empty()){
        StringFormat csf;
        csf.SetAlignment(StringAlignmentCenter);
        csf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush tb(Color(140, tc.GetR(), tc.GetG(), tc.GetB()));
        DrawTextCrisp(g, L"（空文件夹）", g_fontUi, body, &csf, &tb);
        g.ResetClip();
        return;
    }

    int n = (int)g_rows.size();
    for (int i = 0; i < n; i++){
        int y = th - g_scroll + i * rh;
        if (y + rh < th || y > h) continue;

        Row& row = g_rows[i];
        Item* it = row.item;
        int x = LeftPad() + row.depth * Indent();

        // hover pill
        if (g_hoverRow == i){
            RectF hr((REAL)x - S(5), (REAL)y + S(2), (REAL)(w - x - S(5)), (REAL)(rh - S(4)));
            GraphicsPath hp;
            RoundedRect(hp, hr, (REAL)S(6));
            Color ac = AccentHi();
            LinearGradientBrush hb(hr,
                                   Color(58, ac.GetR(), ac.GetG(), ac.GetB()),
                                   Color(30, ac.GetR(), ac.GetG(), ac.GetB()),
                                   LinearGradientModeHorizontal);
            g.FillPath(&hb, &hp);
            Pen hpen(Color(96, ac.GetR(), ac.GetG(), ac.GetB()), 1.0f);
            g.DrawPath(&hpen, &hp);
        }

        int iconX = x;
        // chevron for directories
        if (it->isDir){
            RectF ar((REAL)x, (REAL)y, (REAL)S(15), (REAL)rh);
            StringFormat asf;
            asf.SetAlignment(StringAlignmentCenter);
            asf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush ab(Color(180, tc.GetR(), tc.GetG(), tc.GetB()));
            DrawTextCrisp(g, it->expanded ? L"\uE70D" : L"\uE76C", g_fontGlyphSmall, ar, &asf, &ab);
            iconX = x + S(16);
        }

        // icon
        Bitmap* icon = IconFor(it);
        if (icon){
            int isz = S(18);
            g.DrawImage(icon, iconX, y + (rh - isz) / 2, isz, isz);
        } else {
            SolidBrush fb(Color(200, tc.GetR(), tc.GetG(), tc.GetB()));
            RectF fr((REAL)iconX, (REAL)y + 2, (REAL)S(18), (REAL)(rh - 4));
            StringFormat fs2;
            fs2.SetAlignment(StringAlignmentCenter);
            fs2.SetLineAlignment(StringAlignmentCenter);
            DrawTextCrisp(g, it->isDir ? L"\uE8B7" : L"\uE7C3", g_fontGlyph, fr, &fs2, &fb);
        }

        // text
        int tx = iconX + S(18) + S(8);
        RectF tr((REAL)tx, (REAL)y, (REAL)(w - tx - S(14)), (REAL)rh);
        StringFormat tsf;
        tsf.SetTrimming(StringTrimmingEllipsisCharacter);
        tsf.SetFormatFlags(StringFormatFlagsNoWrap);
        tsf.SetLineAlignment(StringAlignmentCenter);
        DrawStringShadow(g, it->DisplayName(), g_fontUi, tr, &tsf, tc);
    }

    // (the oversized watermark folder glyph that used to fill dead space was removed)
    g.ResetClip();
}

static void DrawScrollbar(Graphics& g, int w, int h){
    int th = TitleH();
    int vis = h - th;
    int total = (int)g_rows.size() * RowH();
    if (total <= vis) return;

    int barW = S(4);
    int top = th + S(4);
    int trackH = vis - S(8);
    if (trackH < S(20)) return;

    int thumbH = (int)((float)trackH * (float)vis / (float)total);
    if (thumbH < S(26)) thumbH = S(26);
    int maxS = total - vis;
    int pos = maxS > 0 ? (int)((float)g_scroll * (trackH - thumbH) / (float)maxS) : 0;

    RectF r((REAL)(w - S(7)), (REAL)(top + pos), (REAL)barW, (REAL)thumbH);
    GraphicsPath p;
    RoundedRect(p, r, (REAL)(barW / 2.0f));
    Color ac = AccentHi();
    LinearGradientBrush b(RectF(r.X, r.Y, r.X + barW, r.Y + thumbH),
                          Color(150, ac.GetR(), ac.GetG(), ac.GetB()),
                          Color(96, ac.GetR(), ac.GetG(), ac.GetB()),
                          LinearGradientModeVertical);
    g.FillPath(&b, &p);
}

static void DrawFrame(Graphics& g, int w, int h){
    float radius = S(12);

    // border: even, subtle all the way around — no top-to-bottom brightness gradient and
    // no height-proportional sheen/specular bands. Those bands were the source of the
    // faint horizontal colour-difference line that grew more visible the taller the
    // widget got (their bottom edge is a hard alpha boundary at a fixed fraction of h).
    RectF r((REAL)0.5f, (REAL)0.5f, (REAL)(w - 1), (REAL)(h - 1));
    GraphicsPath p;
    RoundedRect(p, r, radius);
    Pen pen(Color(70, 255, 255, 255), 1.0f);
    g.DrawPath(&pen, &p);

    // a second, fainter inner ring adds a beveled edge so the slab reads as thick glass
    RectF r2((REAL)1.5f, (REAL)1.5f, (REAL)(w - 3), (REAL)(h - 3));
    GraphicsPath p2;
    RoundedRect(p2, r2, radius - 1.0f);
    Pen pen2(Color(22, 255, 255, 255), 1.0f);
    g.DrawPath(&pen2, &p2);
}

// ---------------- frost overlay (removed) ----------------
// The "blur strength" slider and its frosted-grain overlay have been removed: the
// widget is true see-through (per-pixel alpha), so DWM shows the live wallpaper behind
// it unmodified. There is no grain texture to draw anymore.

static void PaintAll(Graphics& g, int w, int h){
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    // Grayscale anti-aliasing, NOT ClearType. ClearType lays out red/blue sub-pixels
    // that, once premultiplied into the per-pixel-alpha layered surface, show up as a
    // coloured fringe ("heavy black/coloured edge") around every glyph. Grayscale AA
    // keeps a single clean alpha ramp with no chromatic bleed.
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    // TRUE see-through mode: the background is filled with fully-transparent alpha, so
    // the compositor lets whatever is behind the window show through directly — a static
    // wallpaper, Wallpaper Engine's animated scene, anything. The foreground (text, icons,
    // frame, optional tint) is drawn opaque on top. This is per-pixel alpha via
    // UpdateLayeredWindow, so there is no colour-key and no magenta fringing.
    SolidBrush bg(Color(0, 0, 0, 0));
    g.FillRectangle(&bg, 0, 0, w, h);

    // optional translucent accent tint on top of the see-through area, driven by the
    // opacity slider (0 = pure see-through, 100 = a solid accent slab). Drawn AFTER the
    // colour-key fill so it is NOT keyed out; its own alpha controls how much desktop
    // shows through underneath it. Drawn with the SAME rounded corners as the frame so
    // the corners line up exactly (a square tint under a rounded frame was the source of
    // the visible corner "misalignment").
    if (cfg.opacity > 0){
        BYTE a = (BYTE)(cfg.opacity * 255 / 100);
        if (a > 0){
            GraphicsPath tp;
            RoundedRect(tp, RectF(0, 0, (REAL)w, (REAL)h), (REAL)S(12));
            SolidBrush tint(Color(a, GetRValue(cfg.accent), GetGValue(cfg.accent), GetBValue(cfg.accent)));
            g.FillPath(&tint, &tp);
        }
    }

    // frosted grain ("blur" slider) removed — the see-through glass is now clean.

    DrawList(g, w, h);
    DrawScrollbar(g, w, h);
    DrawTitle(g, w);
    DrawFrame(g, w, h);

    // resize handle hint
    RECT rc = ResizeCornerRect(w, h);
    Color tc = TextColor();
    Pen hp(Color(105, tc.GetR(), tc.GetG(), tc.GetB()), 1.2f);
    int m = S(6);
    g.DrawLine(&hp, (REAL)(rc.right - m), (REAL)(rc.bottom - 2), (REAL)(rc.right - 2), (REAL)(rc.bottom - m));
    g.DrawLine(&hp, (REAL)(rc.right - m + S(4)), (REAL)(rc.bottom - 2), (REAL)(rc.right - 2), (REAL)(rc.bottom - m + S(4)));
    g.DrawLine(&hp, (REAL)(rc.right - m + S(8)), (REAL)(rc.bottom - 2), (REAL)(rc.right - 2), (REAL)(rc.bottom - m + S(8)));
}

// ---------------- actions ----------------
static void OpenItem(Item* it){
    ShellExecuteW(NULL, L"open", it->path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static void Toggle(Item* it){
    if (!it->isDir) return;
    it->expanded = !it->expanded;
    RebuildRows();
    RECT r; GetClientRect(g_hwnd, &r);
    ClampScroll(r.right, r.bottom);
    Repaint();
}

static void Reload(){
    LoadRoot();
    RECT r; GetClientRect(g_hwnd, &r);
    g_scroll = 0;
    ClampScroll(r.right, r.bottom);
    Repaint();
}

// ---------------- desktop hosting ----------------
// A desktop gadget must sit *on* the desktop: above the wallpaper, below every
// ordinary application window, never taking focus, and surviving "show desktop".
//
// The reliable anchor is the shell's wallpaper window (Progman / WorkerW). On
// Win10/11 Explorer paints the wallpaper into a top-level "WorkerW" that sits below
// the desktop-icon layer (SHELLDLL_DefView) which in turn sits below ordinary app
// windows. Placing our window *immediately after* the WorkerW in the Z-order pins it
// exactly where a desktop gadget belongs: above the wallpaper, below the icons and
// every app. HWND_BOTTOM is NOT used as the primary strategy because DWM can park the
// window *below* the wallpaper, making it invisible ("won't show up") until some
// random z-order shuffle nudges it back — the intermittent "sometimes it appears"
// symptom. We only fall back to HWND_BOTTOM when no WorkerW is present (rare).

// find the shell wallpaper window that hosts the desktop picture
static HWND GetDesktopWorkerW(){
    // On Win10/11 the desktop icons (SHELLDLL_DefView -> SysListView32) are children
    // of the top-level Progman window, and the wallpaper is drawn by a WorkerW child
    // of Progman. Parking our window immediately AFTER Progman in the z-order puts it
    // above the wallpaper (and the icons, which are Progman's children) but below every
    // ordinary app window — exactly where a desktop gadget belongs.
    HWND progman = FindWindowW(L"Progman", NULL);
    if (progman) return progman;

    // fallback for shell restarts where Progman is temporarily gone: try a WorkerW that
    // hosts a DefView, else any top-level WorkerW.
    HWND w = NULL;
    while ((w = FindWindowExW(NULL, w, L"WorkerW", NULL)) != NULL){
        if (FindWindowExW(w, NULL, L"SHELLDLL_DefView", NULL)) return w;
    }
    HWND any = FindWindowW(L"WorkerW", NULL);
    if (any) return any;
    return NULL;
}

static void SitOnDesktop(){
    if (!g_hwnd) return;
    // keep it a plain top-level tool window that never takes focus
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    ex &= ~WS_EX_TOPMOST;
    ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);

    // IMPORTANT: SetWindowPos(hwnd, hInsertAfter) places our window *below* hInsertAfter
    // in the z-order (hInsertAfter is the "insert after = go under" anchor). To park the
    // widget ABOVE the wallpaper host we must target the window that sits immediately
    // ABOVE the host, not the host itself. GW_HWNDPREV gives us that window.
    HWND host = GetDesktopWorkerW();   // Progman (or a WorkerW) = the wallpaper layer
    HWND insertAfter = HWND_BOTTOM;
    if (host){
        insertAfter = GetWindow(host, GW_HWNDPREV);   // window just above the wallpaper
        if (!insertAfter) insertAfter = HWND_TOP;      // host is already topmost in its band
    }

    // SWP_SHOWWINDOW makes this a show+position in one atomic call, so a window that
    // got hidden (or buried below the wallpaper) is restored and re-anchored together,
    // avoiding the "sometimes it appears, sometimes it doesn't" race.
    SetWindowPos(g_hwnd, insertAfter, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// temporarily allow activation so a popup menu can take focus (needed for click-away)
static void SetNoActivate(bool on){
    if (!g_hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if (on) ex |= WS_EX_NOACTIVATE; else ex &= ~WS_EX_NOACTIVATE;
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
}

// ---------------- folder picker ----------------
static bool PickFolder(HWND owner, std::wstring& out){
    IFileOpenDialog* dlg = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                IID_IFileOpenDialog, (void**)&dlg))) return false;
    DWORD opts = 0; dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))){
        IShellItem* item = NULL;
        if (SUCCEEDED(dlg->GetResult(&item))){
            PWSTR p = NULL;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))){
                out = p; CoTaskMemFree(p); ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

// ---------------- double buffering ----------------
struct BackBuffer { HDC dc = NULL; HBITMAP bmp = NULL; HGDIOBJ old = NULL; int w = 0, h = 0; };
static BackBuffer g_bbMain, g_bbSet;

static HDC GetBackBuffer(BackBuffer& bb, HDC ref, int w, int h){
    if (bb.dc && bb.w == w && bb.h == h) return bb.dc;
    if (bb.bmp){
        SelectObject(bb.dc, bb.old);
        DeleteObject(bb.bmp);
        bb.bmp = NULL;
    }
    if (!bb.dc) bb.dc = CreateCompatibleDC(ref);
    bb.bmp = CreateCompatibleBitmap(ref, w, h);
    bb.old = SelectObject(bb.dc, bb.bmp);
    bb.w = w; bb.h = h;
    return bb.dc;
}

// ---------------- per-pixel alpha layered surface (true see-through) ----------------
// The widget is drawn into a 32-bit premultiplied-ARGB DIB section and submitted with
// UpdateLayeredWindow. Background pixels get alpha=0 (fully transparent), so whatever is
// behind the window — a static wallpaper or Wallpaper Engine's live scene — shows through
// with full DWM compositing fidelity. This is the same mechanism Rainmeter/TranslucentTB
// use, and it is reliable where the legacy colour-key API fails under Wallpaper Engine.
struct LayeredSurface {
    HDC dc = NULL;
    HBITMAP bmp = NULL;
    HGDIOBJ old = NULL;
    void* bits = NULL;
    int w = 0, h = 0;
};
// GDI+ stores its own bitmap surfaces as PREMULTIPLIED alpha. When we draw into a
// DIB section through an HDC, GDI+ has no alpha channel to write to, so it approximates
// the blend against the (black) backdrop and leaves the alpha byte holding the raw
// coverage — but the RGB bytes end up PREMULTIPLIED already. Proof from the probe:
// a half-covered white glyph edge came out as (136,135,135,154) — i.e. 250 * 154/255
// ≈ 136 — which is exactly premultiplied. Running our own premultiply pass on top of
// that multiplied by alpha a SECOND time, crushing every anti-aliased edge to a dark
// grey. That is what made the thin list-row text look grey, blurry and unreadable
// while the bold title (whose strokes are mostly alpha=255) stayed crisp.
//
// The fix: render into a real GDI+ 32bppPARGB bitmap. GDI+ then owns the whole
// alpha compositing, and LockBits(PixelFormat32bppPARGB) hands back pixels that are
// already premultiplied exactly once — which is precisely what UpdateLayeredWindow
// wants. No extra premultiply pass, no double-darkening, no ambiguity.
static LayeredSurface g_ls;
static Bitmap* g_lsPaint = NULL;    // PARGB bitmap the widget is rendered into

// make sure the PARGB paint surface exists at the requested size
static Bitmap* GetPaintBitmap(int w, int h){
    if (g_lsPaint && g_lsPaint->GetWidth() == w && g_lsPaint->GetHeight() == h)
        return g_lsPaint;
    if (g_lsPaint){ delete g_lsPaint; g_lsPaint = NULL; }
    g_lsPaint = new Bitmap(w, h, PixelFormat32bppPARGB);
    return g_lsPaint;
}

// when a resize is in flight, CommitLayered moves+resizes the window atomically via
// UpdateLayeredWindow (pptDst/psize) instead of SetWindowPos, so the surface and window
// rect stay in lock-step and no double-image ghost appears.
static bool g_resizeHasPos = false;
static POINT g_resizePos = {0, 0};

static void FreeLayeredSurface(){
    if (g_ls.dc){
        if (g_ls.bmp) SelectObject(g_ls.dc, g_ls.old);
        DeleteDC(g_ls.dc);
        g_ls.dc = NULL;
    }
    if (g_ls.bmp){ DeleteObject(g_ls.bmp); g_ls.bmp = NULL; }
    g_ls.bits = NULL;
    g_ls.w = g_ls.h = 0;
}

// (re)allocate the 32-bit ARGB surface at the requested size and zero it (fully transparent)
static HDC GetLayeredSurface(int w, int h){
    if (g_ls.dc && g_ls.w == w && g_ls.h == h){
        memset(g_ls.bits, 0, (size_t)w * h * 4);
        return g_ls.dc;
    }
    FreeLayeredSurface();
    HDC ref = GetDC(NULL);
    g_ls.dc = CreateCompatibleDC(ref);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_ls.bmp = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &g_ls.bits, NULL, 0);
    ReleaseDC(NULL, ref);
    if (!g_ls.bmp){ FreeLayeredSurface(); return NULL; }
    g_ls.old = SelectObject(g_ls.dc, g_ls.bmp);
    g_ls.w = w; g_ls.h = h;
    memset(g_ls.bits, 0, (size_t)w * h * 4);
    return g_ls.dc;
}

// Copy the rendered PARGB surface into the DIB section that UpdateLayeredWindow reads.
//
// The PARGB bitmap's pixels are ALREADY premultiplied exactly once (that is what
// PixelFormat32bppPARGB means), so no colour multiplication happens here — doing it
// again is what used to darken every anti-aliased glyph edge.
//
// The only fix-up is the alpha clamp: the OS mouse hit-test treats alpha==0 pixels on a
// layered window as HTTRANSPARENT and lets the click fall through to whatever is behind
// (a desktop icon the widget is parked on). WM_NCHITTEST is NOT delivered for such
// pixels, so it cannot be intercepted. Clamping alpha 0 -> 1 (1/255 is invisible to the
// eye, the live desktop still shows through) makes the pixel "opaque" enough that the
// hit-test routes the click to our window instead of leaking it.
static void CopyPaintToDIB(Bitmap* paint, int w, int h){
    if (!g_ls.bits || !paint) return;
    BitmapData bd;
    Rect rc(0, 0, w, h);
    if (paint->LockBits(&rc, ImageLockModeRead, PixelFormat32bppPARGB, &bd) != Ok) return;
    BYTE* src = (BYTE*)bd.Scan0;
    BYTE* dst = (BYTE*)g_ls.bits;
    int srcStride = bd.Stride;
    int dstStride = w * 4;
    for (int y = 0; y < h; y++){
        BYTE* s = src + (size_t)y * srcStride;
        BYTE* d = dst + (size_t)y * dstStride;
        memcpy(d, s, dstStride);
        for (int x = 0; x < w; x++){
            if (d[x*4+3] == 0) d[x*4+3] = 1;   // defeat per-pixel hit-test transparency leak
        }
    }
    paint->UnlockBits(&bd);
}

static void CommitLayered(){
    if (!g_hwnd) return;
    int w, h;
    if (g_resizeHasPos){
        // mid-resize: the window rect hasn't been updated yet (we move+size atomically via
        // UpdateLayeredWindow below), so draw at the target size we already computed.
        w = cfg.width; h = cfg.height;
    } else {
        RECT rc; GetClientRect(g_hwnd, &rc);
        w = rc.right; h = rc.bottom;
    }
    if (w <= 0 || h <= 0) return;
    HDC surf = GetLayeredSurface(w, h);
    if (!surf) return;
    Bitmap* paint = GetPaintBitmap(w, h);
    if (!paint) return;
    {
        Graphics g(paint);
        g.Clear(Color(0, 0, 0, 0));
        g_paintDst = paint;              // the crisp text renderer writes here
        PaintAll(g, w, h);
        g_paintDst = NULL;
        g.Flush();
    }
    CopyPaintToDIB(paint, w, h);
    POINT src = {0, 0};
    SIZE sz = {w, h};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(NULL);
    // Pass the window's actual screen position explicitly. During a resize we let
    // UpdateLayeredWindow move+size the window in one atomic call (g_resizeHasPos), so the
    // freshly-painted surface is pinned to the exact rect it was drawn for — this is what
    // prevents the "double image" ghosting when dragging an edge.
    POINT dst;
    if (g_resizeHasPos){ dst = g_resizePos; }
    else {
        RECT wr; GetWindowRect(g_hwnd, &wr);
        dst.x = wr.left; dst.y = wr.top;
    }
    UpdateLayeredWindow(g_hwnd, screen, &dst, &sz, surf, &src, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, screen);
}

// A layered (UpdateLayeredWindow) window does NOT receive WM_PAINT and does not redraw
// from InvalidateRect — it redraws only when UpdateLayeredWindow is called. Everywhere
// the old GDI code called InvalidateRect(g_hwnd) to trigger a repaint, we now call this,
// which immediately re-renders the surface and submits it.
static void Repaint(){ CommitLayered(); }

static void PaintSettings(Graphics& g, int w, int h);   // fwd decl (defined below)

// ---------------- settings panel layered surface (true see-through) ----------------
// The settings panel is also a per-pixel-alpha layered window, so its fixed translucent
// tint composites over the live desktop (dynamic wallpaper included) exactly like the
// main widget. It has its own surface/commit helpers so the two windows never clash.
static LayeredSurface g_setLs;
static Bitmap* g_setPaint = NULL;   // PARGB bitmap the settings panel is rendered into

static void FreeSetLayeredSurface(){
    if (g_setLs.dc){
        if (g_setLs.bmp) SelectObject(g_setLs.dc, g_setLs.old);
        DeleteDC(g_setLs.dc);
        g_setLs.dc = NULL;
    }
    if (g_setLs.bmp){ DeleteObject(g_setLs.bmp); g_setLs.bmp = NULL; }
    g_setLs.bits = NULL;
    g_setLs.w = g_setLs.h = 0;
}

static HDC GetSetLayeredSurface(int w, int h){
    if (g_setLs.dc && g_setLs.w == w && g_setLs.h == h){
        memset(g_setLs.bits, 0, (size_t)w * h * 4);
        return g_setLs.dc;
    }
    FreeSetLayeredSurface();
    HDC ref = GetDC(NULL);
    g_setLs.dc = CreateCompatibleDC(ref);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_setLs.bmp = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &g_setLs.bits, NULL, 0);
    ReleaseDC(NULL, ref);
    if (!g_setLs.bmp){ FreeSetLayeredSurface(); return NULL; }
    g_setLs.old = SelectObject(g_setLs.dc, g_setLs.bmp);
    g_setLs.w = w; g_setLs.h = h;
    memset(g_setLs.bits, 0, (size_t)w * h * 4);
    return g_setLs.dc;
}

static Bitmap* GetSetPaintBitmap(int w, int h){
    if (g_setPaint && g_setPaint->GetWidth() == w && g_setPaint->GetHeight() == h)
        return g_setPaint;
    if (g_setPaint){ delete g_setPaint; g_setPaint = NULL; }
    g_setPaint = new Bitmap(w, h, PixelFormat32bppPARGB);
    return g_setPaint;
}

static void CommitSetLayered(){
    if (!g_setHwnd) return;
    RECT rc; GetClientRect(g_setHwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    HDC surf = GetSetLayeredSurface(w, h);
    if (!surf) return;
    Bitmap* paint = GetSetPaintBitmap(w, h);
    if (!paint) return;
    {
        Graphics g(paint);
        g.Clear(Color(0, 0, 0, 0));
        g_paintDst = paint;              // the crisp text renderer writes here
        PaintSettings(g, w, h);
        g_paintDst = NULL;
        g.Flush();
    }
    // copy the (already premultiplied) PARGB pixels into the DIB section
    if (g_setLs.bits){
        BitmapData bd;
        Rect r2(0, 0, w, h);
        if (paint->LockBits(&r2, ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Ok){
            BYTE* src = (BYTE*)bd.Scan0;
            BYTE* dst = (BYTE*)g_setLs.bits;
            for (int y = 0; y < h; y++){
                BYTE* s = src + (size_t)y * bd.Stride;
                BYTE* d = dst + (size_t)y * w * 4;
                memcpy(d, s, (size_t)w * 4);
                for (int x = 0; x < w; x++){
                    if (d[x*4+3] == 0) d[x*4+3] = 1;
                }
            }
            paint->UnlockBits(&bd);
        }
    }
    POINT src = {0, 0};
    SIZE sz = {w, h};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(NULL);
    UpdateLayeredWindow(g_setHwnd, screen, NULL, &sz, surf, &src, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, screen);
}

// repaint the settings panel: re-render its surface and submit it (layered windows do
// not redraw from InvalidateRect, so every InvalidateRect on this window is routed here)
static void RepaintSet(){ CommitSetLayered(); }

// ---------------- settings panel (custom-drawn frosted glass) ----------------
static const COLORREF g_presets[6] = {
    RGB(128, 128, 128), RGB(30, 30, 30), RGB(245, 245, 245),
    RGB(43, 108, 176), RGB(31, 162, 166), RGB(107, 91, 210)
};

// logical layout (multiplied by DPI scale through S())
static const int SET_W = 400;
static const int SET_H = 548;

static RECT LR(int x, int y, int w, int h){ return {S(x), S(y), S(x + w), S(y + h)}; }

static bool InRect(const RECT& r, POINT p){
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

static RECT R_SetClose(){ return LR(340, 10, 28, 28); }
static RECT R_FolderCard(){ return LR(16, 60, 368, 44); }
static RECT R_FolderChange(){ return LR(306, 66, 68, 32); }
static int  SliderTrackY(int i){ int y[3] = {162, 214, 266}; return S(y[i]); }
static RECT R_SliderHit(int i){ int y[3] = {162, 214, 266}; return LR(18, y[i] - 12, 364, 24); }
static int  SliderLabelY(int i){ int y[3] = {140, 192, 244}; return S(y[i]); }
static RECT R_Preset(int i){ return LR(20 + i * 38, 366, 30, 30); }
static RECT R_HexBox(){ return LR(250, 366, 84, 30); }
static RECT R_CustomColor(){ return LR(338, 366, 30, 30); }
static RECT R_CheckAuto(){ return LR(18, 424, 344, 24); }
static RECT R_HzLabel(){ return LR(20, 450, 120, 20); }
static RECT R_HzOption(int i){ return LR(110 + i * 44, 448, 38, 24); }
static RECT R_Reset(){ return LR(20, 492, 100, 34); }
static RECT R_Done(){ return LR(284, 492, 84, 34); }

// refresh-rate presets the user can pick from (Hz)
static const int g_hzPresets[6] = { 60, 90, 120, 144, 165, 240 };

enum { SH_NONE = 0, SH_CLOSE, SH_FOLDER, SH_SLIDER0, SH_SLIDER1, SH_SLIDER2,
       SH_P0, SH_P1, SH_P2, SH_P3, SH_P4, SH_P5, SH_CUSTOM, SH_HEX, SH_AUTO,
       SH_HZ0, SH_HZ1, SH_HZ2, SH_HZ3, SH_HZ4, SH_HZ5, SH_RESET, SH_DONE };

static int  g_setHover = SH_NONE;
static int  g_setActive = SH_NONE;
static bool g_setDragTitle = false;
static POINT g_setDragOff = {0, 0};

// hex colour input box (e.g. "#FF0066")
static bool g_hexEditing = false;
static std::wstring g_hexBuf;

static bool IsDarkText(Color c){ return c.GetR() < 128; }
static float LumColor(Color c){
    return (0.299f * c.GetR() + 0.587f * c.GetG() + 0.114f * c.GetB()) / 255.0f;
}
static Color HsvToRgb(float h, float s, float v){
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if      (h < 60.0f){ r = c; g = x; }
    else if (h < 120.0f){ r = x; g = c; }
    else if (h < 180.0f){ g = c; b = x; }
    else if (h < 240.0f){ g = x; b = c; }
    else if (h < 300.0f){ r = x; b = c; }
    else { r = c; b = x; }
    return Color(255, (BYTE)((r + m) * 255), (BYTE)((g + m) * 255), (BYTE)((b + m) * 255));
}
// keep the user's hue but make sure controls pop against the glass
static Color Vivid(Color c){
    float r = c.GetR() / 255.0f, g = c.GetG() / 255.0f, b = c.GetB() / 255.0f;
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    float d = mx - mn;
    float h = 210.0f, s = (mx > 0.001f) ? d / mx : 0.0f, v = mx;
    if (d > 0.001f){
        if (mx == r)      h = 60.0f * fmodf((g - b) / d, 6.0f);
        else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
        else              h = 60.0f * ((r - g) / d + 4.0f);
        if (h < 0) h += 360.0f;
    }
    if (s < 0.30f) s = 0.52f;
    if (v < 0.55f) v = 0.70f;
    if (v > 0.92f) v = 0.88f;
    return HsvToRgb(h, s, v);
}
// a control colour that stays visible on top of the glass, whatever accent the user picked
static Color ControlColor(Color tc){
    if (!IsDarkText(tc)) return Vivid(AccentSolid());       // dark glass -> vivid accent
    Color a = AccentSolid();                                // light glass -> darkened accent
    Color d((BYTE)245, (BYTE)(a.GetR() * 55 / 100), (BYTE)(a.GetG() * 55 / 100), (BYTE)(a.GetB() * 55 / 100));
    if (LumColor(d) > 0.55f) return Color(255, 42, 46, 54);
    return d;
}
static Color CardFill(Color tc){ return IsDarkText(tc) ? Color(26, 0, 0, 0) : Color(44, 255, 255, 255); }
static Color CardBorder(Color tc){ return IsDarkText(tc) ? Color(42, 0, 0, 0) : Color(70, 255, 255, 255); }
static Color TextOn(Color c){
    float l = (0.299f * c.GetR() + 0.587f * c.GetG() + 0.114f * c.GetB()) / 255.0f;
    return l > 0.62f ? Color(255, 24, 24, 28) : Color(255, 255, 255, 255);
}

static void DrawSlider(Graphics& g, int y, int v, int mn, int mx, Color hi, bool active){
    int x0 = S(20), x1 = S(380);
    float t = (mx == mn) ? 0.0f : (float)(v - mn) / (float)(mx - mn);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float kx = (float)x0 + t * (float)(x1 - x0);
    float th = (float)S(6);

    RectF tr((REAL)x0, (REAL)y - th * 0.5f, (REAL)(x1 - x0), th);
    GraphicsPath tp;
    RoundedRect(tp, tr, th * 0.5f);
    SolidBrush tb(Color(46, 255, 255, 255));
    g.FillPath(&tb, &tp);
    Pen tp2(Color(34, 0, 0, 0), 1.0f);
    g.DrawPath(&tp2, &tp);

    if (kx > (float)x0 + 1.5f){
        RectF fr((REAL)x0, (REAL)y - th * 0.5f, (REAL)(kx - x0), th);
        GraphicsPath fp;
        RoundedRect(fp, fr, th * 0.5f);
        Color base = hi;
        LinearGradientBrush fb(fr,
                               Color((BYTE)(active ? 255 : 235), base.GetR(), base.GetG(), base.GetB()),
                               Color((BYTE)(active ? 225 : 195), base.GetR(), base.GetG(), base.GetB()),
                               LinearGradientModeVertical);
        g.FillPath(&fb, &fp);
    }

    float kr = (float)S(active ? 10 : 9);
    SolidBrush ks(Color(46, 0, 0, 0));
    g.FillEllipse(&ks, kx - kr + 1, (REAL)y - kr + 1, kr * 2, kr * 2);
    SolidBrush kb(Color(253, 255, 255, 255));
    g.FillEllipse(&kb, kx - kr, (REAL)y - kr, kr * 2, kr * 2);
    Pen kp(Color(active ? 235 : 190, hi.GetR(), hi.GetG(), hi.GetB()), active ? 2.0f : 1.2f);
    g.DrawEllipse(&kp, kx - kr, (REAL)y - kr, kr * 2, kr * 2);
}

static void DrawCheckbox(Graphics& g, RECT r, bool on, const wchar_t* label, Color tc, Color hi, bool hover){
    int s = S(18);
    RectF box((REAL)r.left, (REAL)(r.top + (r.bottom - r.top - s) / 2), (REAL)s, (REAL)s);
    GraphicsPath bp;
    RoundedRect(bp, box, (REAL)S(5));
    SolidBrush bf(on ? hi : Color(30, 255, 255, 255));
    g.FillPath(&bf, &bp);
    Pen bp2(on ? Color(255, hi.GetR(), hi.GetG(), hi.GetB()) : Color(hover ? 125 : 80, 255, 255, 255), 1.0f);
    g.DrawPath(&bp2, &bp);
    if (on){
        StringFormat gsf;
        gsf.SetAlignment(StringAlignmentCenter);
        gsf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush gb(TextOn(hi));
        DrawTextCrisp(g, L"\uE73E", g_fontGlyphSmall, box, &gsf, &gb);
    }
    RectF lr((REAL)(r.left + s + S(10)), (REAL)r.top,
             (REAL)(r.right - r.left - s - S(10)), (REAL)(r.bottom - r.top));
    StringFormat lsf;
    lsf.SetFormatFlags(StringFormatFlagsNoWrap);
    lsf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush lb(hover ? tc : Color(200, tc.GetR(), tc.GetG(), tc.GetB()));
    DrawTextCrisp(g, label, g_fontUi, lr, &lsf, &lb);
}

static void DrawSectionLabel(Graphics& g, int x, int y, const wchar_t* text, Color c){
    RectF r((REAL)S(x), (REAL)S(y), (REAL)S(220), (REAL)S(18));
    StringFormat sf;
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush b(c);
    DrawTextCrisp(g, text, g_fontBold, r, &sf, &b);
}

static void PaintSettings(Graphics& g, int w, int h){
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    // TRUE see-through panel, identical in spirit to the main widget: the backdrop is a
    // fixed, low-alpha dark tint (≈20%) drawn as per-pixel alpha. DWM composites whatever
    // is behind us — a static wallpaper or Wallpaper Engine's live scene — through it in
    // real time. There is NO screen capture here, so there is nothing to go stale, no
    // flicker from re-capturing, and no misaligned snapshot of a nearby window (e.g. the
    // "WorkBuddy bleeds into the panel" bug). The tint is a constant, independent of the
    // widget's opacity/blur sliders, so the panel always stays a clean, readable acrylic.
    Color tint = Color(51, 30, 30, 32);   // ~20% alpha dark slate
    {
        GraphicsPath bp;
        RoundedRect(bp, RectF(0, 0, (REAL)w, (REAL)h), (REAL)S(12));
        SolidBrush bb(tint);
        g.FillPath(&bb, &bp);
    }

    Color tc = Color(255, 247, 247, 250);
    Color dim(168, tc.GetR(), tc.GetG(), tc.GetB());
    Color faint(112, tc.GetR(), tc.GetG(), tc.GetB());
    // controls must contrast against the glass: dark accent on light glass, light accent on dark glass
    Color hi = ControlColor(tc);

    // title
    {
        RectF r((REAL)S(20), (REAL)S(8), (REAL)S(240), (REAL)S(36));
        StringFormat sf;
        sf.SetFormatFlags(StringFormatFlagsNoWrap);
        sf.SetLineAlignment(StringAlignmentCenter);
        DrawStringShadow(g, L"设置", g_fontBold, r, &sf, tc);
    }
    // close
    {
        RECT b = R_SetClose();
        RectF br((REAL)b.left, (REAL)b.top, (REAL)(b.right - b.left), (REAL)(b.bottom - b.top));
        if (g_setHover == SH_CLOSE){
            GraphicsPath hp;
            RoundedRect(hp, RectF(br.X - 2, br.Y - 2, br.Width + 4, br.Height + 4), (REAL)S(7));
            SolidBrush hb(Color(52, 255, 255, 255));
            g.FillPath(&hb, &hp);
        }
        StringFormat csf;
        csf.SetAlignment(StringAlignmentCenter);
        csf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush tb(tc);
        DrawTextCrisp(g, L"\uE8BB", g_fontGlyph, br, &csf, &tb);
    }

    // divider
    Pen dp(Color(44, 255, 255, 255), 1.0f);
    g.DrawLine(&dp, (REAL)S(16), (REAL)S(52), (REAL)(w - S(16)), (REAL)S(52));

    // folder card
    {
        RECT c = R_FolderCard();
        RectF cr((REAL)c.left, (REAL)c.top, (REAL)(c.right - c.left), (REAL)(c.bottom - c.top));
        GraphicsPath p;
        RoundedRect(p, cr, (REAL)S(10));
        SolidBrush fill(CardFill(tc));
        g.FillPath(&fill, &p);
        if (g_setHover == SH_FOLDER){
            SolidBrush hv(Color(24, 255, 255, 255));
            g.FillPath(&hv, &p);
        }
        Pen bp(g_setHover == SH_FOLDER ? Color(130, 255, 255, 255) : CardBorder(tc), 1.0f);
        g.DrawPath(&bp, &p);

        RectF gr((REAL)c.left + S(10), (REAL)c.top, (REAL)S(24), (REAL)(c.bottom - c.top));
        StringFormat gsf;
        gsf.SetAlignment(StringAlignmentCenter);
        gsf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush gb(Color(235, hi.GetR(), hi.GetG(), hi.GetB()));
        DrawTextCrisp(g, L"\uE8B7", g_fontGlyph, gr, &gsf, &gb);

        std::wstring full = cfg.folder;
        while (!full.empty() && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
        size_t slash = full.find_last_of(L"\\/");
        std::wstring name = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
        if (name.empty()) name = full;

        int tx = c.left + S(42);
        int tw = (c.right - tx - S(86));
        RectF nr((REAL)tx, (REAL)c.top + S(5), (REAL)tw, (REAL)S(20));
        StringFormat nsf;
        nsf.SetTrimming(StringTrimmingEllipsisCharacter);
        nsf.SetFormatFlags(StringFormatFlagsNoWrap);
        nsf.SetLineAlignment(StringAlignmentCenter);
        DrawStringShadow(g, name, g_fontBold, nr, &nsf, tc);

        RectF pr((REAL)tx, (REAL)c.top + S(23), (REAL)tw, (REAL)S(17));
        StringFormat psf;
        psf.SetTrimming(StringTrimmingEllipsisPath);
        psf.SetFormatFlags(StringFormatFlagsNoWrap);
        psf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush pb(Color(190, tc.GetR(), tc.GetG(), tc.GetB()));
        DrawTextCrisp(g, cfg.folder.c_str(), g_fontSmall, pr, &psf, &pb);

        RECT ch = R_FolderChange();
        RectF chr((REAL)ch.left, (REAL)ch.top, (REAL)(ch.right - ch.left), (REAL)(ch.bottom - ch.top));
        GraphicsPath cp;
        RoundedRect(cp, chr, (REAL)S(8));
        SolidBrush cb(Color(g_setHover == SH_FOLDER ? 104 : 72, 255, 255, 255));
        g.FillPath(&cb, &cp);
        Pen cpen(Color(70, 255, 255, 255), 1.0f);
        g.DrawPath(&cpen, &cp);
        StringFormat csf;
        csf.SetAlignment(StringAlignmentCenter);
        csf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush ctb(tc);
        DrawTextCrisp(g, L"更改…", g_fontUi, chr, &csf, &ctb);
    }

    // appearance sliders
    DrawSectionLabel(g, 20, 112, L"外观", Color(238, tc.GetR(), tc.GetG(), tc.GetB()));
    {
        const wchar_t* names[3] = { L"宽度", L"高度", L"不透明度" };
        int vals[3] = { cfg.width, cfg.height, cfg.opacity };
        int mins[3] = { 240, 240, 0 };
        int maxs[3] = { 1000, 1200, 100 };
        for (int i = 0; i < 3; i++){
            RectF lr((REAL)S(20), (REAL)SliderLabelY(i), (REAL)S(220), (REAL)S(22));
            StringFormat lsf;
            lsf.SetFormatFlags(StringFormatFlagsNoWrap);
            lsf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush lb(dim);
            DrawTextCrisp(g, names[i], g_fontUi, lr, &lsf, &lb);

            wchar_t buf[48];
            if (i < 2) wsprintfW(buf, L"%d px", vals[i]);
            else wsprintfW(buf, L"%d %%", vals[i]);
            RectF vr((REAL)S(240), (REAL)SliderLabelY(i), (REAL)S(140), (REAL)S(22));
            StringFormat vsf;
            vsf.SetAlignment(StringAlignmentFar);
            vsf.SetFormatFlags(StringFormatFlagsNoWrap);
            vsf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush vb(g_setActive == SH_SLIDER0 + i ? tc : Color(200, tc.GetR(), tc.GetG(), tc.GetB()));
            DrawTextCrisp(g, buf, g_fontBold, vr, &vsf, &vb);

            DrawSlider(g, SliderTrackY(i), vals[i], mins[i], maxs[i], hi, g_setActive == SH_SLIDER0 + i);
        }
    }

    // background color
    DrawSectionLabel(g, 20, 344, L"背景色", Color(238, tc.GetR(), tc.GetG(), tc.GetB()));
    for (int i = 0; i < 6; i++){
        RECT sr = R_Preset(i);
        RectF sr2((REAL)sr.left, (REAL)sr.top, (REAL)(sr.right - sr.left), (REAL)(sr.bottom - sr.top));
        GraphicsPath sp;
        RoundedRect(sp, sr2, (REAL)S(8));
        COLORREF pc = g_presets[i];
        SolidBrush sb(Color(255, GetRValue(pc), GetGValue(pc), GetBValue(pc)));
        g.FillPath(&sb, &sp);
        bool sel = (cfg.accent == pc);
        bool hov = (g_setHover == SH_P0 + i);
        if (sel){
            GraphicsPath ring;
            RoundedRect(ring, RectF(sr2.X - S(3), sr2.Y - S(3), sr2.Width + S(6), sr2.Height + S(6)), (REAL)S(10));
            Pen rp(Color(205, hi.GetR(), hi.GetG(), hi.GetB()), 1.6f);
            g.DrawPath(&rp, &ring);
            StringFormat tsf;
            tsf.SetAlignment(StringAlignmentCenter);
            tsf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush tk(TextOn(Color(255, GetRValue(pc), GetGValue(pc), GetBValue(pc))));
            DrawTextCrisp(g, L"\uE73E", g_fontGlyphSmall, sr2, &tsf, &tk);
        } else {
            Pen pen(Color(hov ? 170 : 96, 255, 255, 255), 1.0f);
            g.DrawPath(&pen, &sp);
        }
    }
    {
        // hex colour input box: type any #RRGGBB and it applies live
        RECT hb = R_HexBox();
        RectF hr((REAL)hb.left, (REAL)hb.top, (REAL)(hb.right - hb.left), (REAL)(hb.bottom - hb.top));
        GraphicsPath hp;
        RoundedRect(hp, hr, (REAL)S(8));
        SolidBrush hfill(Color(g_hexEditing ? 96 : (g_setHover == SH_HEX ? 84 : 60), 255, 255, 255));
        g.FillPath(&hfill, &hp);
        Pen hpen(Color(g_hexEditing ? 190 : (g_setHover == SH_HEX ? 150 : 96), 255, 255, 255), 1.0f);
        g.DrawPath(&hpen, &hp);

        std::wstring hexText = g_hexEditing ? g_hexBuf : (L"#" + HexColor(cfg.accent));
        // show the live buffer (with a caret) while editing, otherwise the current colour
        StringFormat hsf;
        hsf.SetAlignment(StringAlignmentCenter);
        hsf.SetLineAlignment(StringAlignmentCenter);
        hsf.SetFormatFlags(StringFormatFlagsNoWrap);
        SolidBrush htb(Color(235, tc.GetR(), tc.GetG(), tc.GetB()));
        DrawTextCrisp(g, hexText.c_str(), g_fontSmall, hr, &hsf, &htb);
    }
    {
        // system colour-picker button (rainbow droplet)
        RECT cc = R_CustomColor();
        RectF cr((REAL)cc.left, (REAL)cc.top, (REAL)(cc.right - cc.left), (REAL)(cc.bottom - cc.top));
        GraphicsPath cp;
        RoundedRect(cp, cr, (REAL)S(8));
        SolidBrush cb(Color(g_setHover == SH_CUSTOM ? 96 : 62, 255, 255, 255));
        g.FillPath(&cb, &cp);
        Pen pen(Color(g_setHover == SH_CUSTOM ? 170 : 96, 255, 255, 255), 1.0f);
        g.DrawPath(&pen, &cp);
        // draw a small rainbow swatch so the button reads as a colour picker without
        // relying on a specific icon font glyph
        RectF sw((REAL)(cc.left + S(7)), (REAL)(cc.top + S(7)), (REAL)S(16), (REAL)S(16));
        GraphicsPath swp;
        swp.AddEllipse(sw);
        LinearGradientBrush rainbow(sw, Color(255, 255, 80, 80), Color(255, 120, 80, 255), LinearGradientModeForwardDiagonal);
        g.FillPath(&rainbow, &swp);
        Pen swpen(Color(200, 255, 255, 255), 1.0f);
        g.DrawPath(&swpen, &swp);
    }

    // options
    DrawSectionLabel(g, 20, 404, L"选项", Color(238, tc.GetR(), tc.GetG(), tc.GetB()));
    DrawCheckbox(g, R_CheckAuto(), cfg.autostart, L"开机自动启动", tc, hi, g_setHover == SH_AUTO);
    {
        // refresh-rate row: pick how fluid the live drag / resize feels
        RECT hl = R_HzLabel();
        RectF hr((REAL)hl.left, (REAL)hl.top, (REAL)(hl.right - hl.left), (REAL)(hl.bottom - hl.top));
        StringFormat hsf;
        hsf.SetFormatFlags(StringFormatFlagsNoWrap);
        hsf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush hb(dim);
        DrawTextCrisp(g, L"拖动刷新率", g_fontUi, hr, &hsf, &hb);

        for (int i = 0; i < 6; i++){
            RECT o = R_HzOption(i);
            RectF or2((REAL)o.left, (REAL)o.top, (REAL)(o.right - o.left), (REAL)(o.bottom - o.top));
            GraphicsPath op;
            RoundedRect(op, or2, (REAL)S(7));
            bool sel = (cfg.refreshHz == g_hzPresets[i]);
            bool hov = (g_setHover == SH_HZ0 + i);
            if (sel){
                SolidBrush of(hi);
                g.FillPath(&of, &op);
            } else {
                SolidBrush of(Color(hov ? 60 : 34, 255, 255, 255));
                g.FillPath(&of, &op);
            }
            Pen ope(Color(hov || sel ? 170 : 70, 255, 255, 255), 1.0f);
            g.DrawPath(&ope, &op);
            wchar_t buf[16];
            wsprintfW(buf, L"%d", g_hzPresets[i]);
            StringFormat osf;
            osf.SetAlignment(StringAlignmentCenter);
            osf.SetLineAlignment(StringAlignmentCenter);
            osf.SetFormatFlags(StringFormatFlagsNoWrap);
            SolidBrush otb(sel ? TextOn(hi) : (hov ? tc : dim));
            DrawTextCrisp(g, buf, g_fontSmall, or2, &osf, &otb);
        }
    }
    {
        SolidBrush nb(Color(150, tc.GetR(), tc.GetG(), tc.GetB()));
        RectF nr((REAL)S(46), (REAL)S(470), (REAL)S(330), (REAL)S(18));
        StringFormat nsf;
        nsf.SetFormatFlags(StringFormatFlagsNoWrap);
        nsf.SetLineAlignment(StringAlignmentCenter);
        DrawTextCrisp(g, L"桌面小组件 · 只在桌面显示，不会盖住其他窗口", g_fontSmall, nr, &nsf, &nb);
    }

    // bottom buttons
    {
        Pen dline(Color(40, 255, 255, 255), 1.0f);
        g.DrawLine(&dline, (REAL)S(16), (REAL)S(478), (REAL)(w - S(16)), (REAL)S(478));

        RECT rr = R_Reset();
        RectF rrf((REAL)rr.left, (REAL)rr.top, (REAL)(rr.right - rr.left), (REAL)(rr.bottom - rr.top));
        GraphicsPath rp;
        RoundedRect(rp, rrf, (REAL)S(9));
        SolidBrush rb(Color(g_setHover == SH_RESET ? 68 : 38, 255, 255, 255));
        g.FillPath(&rb, &rp);
        Pen rpen(Color(g_setHover == SH_RESET ? 110 : 70, 255, 255, 255), 1.0f);
        g.DrawPath(&rpen, &rp);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush tb(g_setHover == SH_RESET ? tc : Color(210, tc.GetR(), tc.GetG(), tc.GetB()));
        DrawTextCrisp(g, L"恢复默认", g_fontUi, rrf, &sf, &tb);
    }
    {
        RECT dr = R_Done();
        RectF dfr((REAL)dr.left, (REAL)dr.top, (REAL)(dr.right - dr.left), (REAL)(dr.bottom - dr.top));
        GraphicsPath dp2;
        RoundedRect(dp2, dfr, (REAL)S(9));
        SolidBrush db(hi);
        g.FillPath(&db, &dp2);
        if (g_setHover == SH_DONE){
            SolidBrush ov(Color(26, 255, 255, 255));
            g.FillPath(&ov, &dp2);
        }
        Pen pen(Color(120, 255, 255, 255), 1.0f);
        g.DrawPath(&pen, &dp2);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush tb(TextOn(hi));
        DrawTextCrisp(g, L"完成", g_fontUi, dfr, &sf, &tb);
    }

    // live-apply hint between the two bottom buttons
    {
        SolidBrush hb(Color(140, tc.GetR(), tc.GetG(), tc.GetB()));
        RectF hr((REAL)S(128), (REAL)S(492), (REAL)S(150), (REAL)S(34));
        StringFormat hf;
        hf.SetAlignment(StringAlignmentCenter);
        hf.SetLineAlignment(StringAlignmentCenter);
        hf.SetFormatFlags(StringFormatFlagsNoWrap);
        DrawTextCrisp(g, L"改动即时生效", g_fontSmall, hr, &hf, &hb);
    }

    // sheen + frame
    {
        float radius = S(12);
        GraphicsPath clip;
        RoundedRect(clip, RectF(0, 0, (REAL)w, (REAL)h), radius);
        g.SetClip(&clip);
        RectF sheen(0, 0, (REAL)w, (REAL)(h * 0.34f));
        LinearGradientBrush lg(sheen, Color(30, 255, 255, 255), Color(0, 255, 255, 255), LinearGradientModeVertical);
        g.FillRectangle(&lg, sheen);
        float sh = h * 0.3f;
        RectF shade(0, (REAL)(h - sh), (REAL)w, (REAL)sh);
        LinearGradientBrush lg2(shade, Color(0, 0, 0, 0), Color(44, 0, 0, 0), LinearGradientModeVertical);
        g.FillRectangle(&lg2, shade);
        g.ResetClip();

        GraphicsPath p;
        RoundedRect(p, RectF(0.5f, 0.5f, (REAL)(w - 1), (REAL)(h - 1)), radius);
        LinearGradientBrush lb(RectF(0, 0, (REAL)w, (REAL)h),
                               Color(135, 255, 255, 255), Color(58, 255, 255, 255),
                               LinearGradientModeVertical);
        Pen pen(&lb, 1.0f);
        g.DrawPath(&pen, &p);
    }
}

static int SetHit(POINT p){
    if (InRect(R_SetClose(), p)) return SH_CLOSE;
    if (InRect(R_FolderCard(), p)) return SH_FOLDER;
    for (int i = 0; i < 3; i++) if (InRect(R_SliderHit(i), p)) return SH_SLIDER0 + i;
    for (int i = 0; i < 6; i++) if (InRect(R_Preset(i), p)) return SH_P0 + i;
    if (InRect(R_HexBox(), p)) return SH_HEX;
    if (InRect(R_CustomColor(), p)) return SH_CUSTOM;
    if (InRect(R_CheckAuto(), p)) return SH_AUTO;
    for (int i = 0; i < 6; i++) if (InRect(R_HzOption(i), p)) return SH_HZ0 + i;
    if (InRect(R_Reset(), p)) return SH_RESET;
    if (InRect(R_Done(), p)) return SH_DONE;
    return SH_NONE;
}

static void SetSliderFromX(int id, int x){
    int x0 = S(20), x1 = S(380);
    float t = (x1 > x0) ? (float)(x - x0) / (float)(x1 - x0) : 0.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (id == SH_SLIDER0) cfg.width = 240 + (int)(t * 760.0f + 0.5f);
    else if (id == SH_SLIDER1) cfg.height = 240 + (int)(t * 960.0f + 0.5f);
    else if (id == SH_SLIDER2) cfg.opacity = (int)(t * 100.0f + 0.5f);
}

// keep the widget reachable when it is enlarged near a screen edge
static void KeepOnScreen(){
    if (!g_hwnd) return;
    RECT wr; GetWindowRect(g_hwnd, &wr);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int nx = wr.left, ny = wr.top;
    if (wr.right  > wa.right)  nx = wa.right  - (wr.right - wr.left);
    if (wr.bottom > wa.bottom) ny = wa.bottom - (wr.bottom - wr.top);
    if (nx < wa.left) nx = wa.left;
    if (ny < wa.top)  ny = wa.top;
    if (nx != wr.left || ny != wr.top)
        SetWindowPos(g_hwnd, NULL, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

// live apply: widget and panel update immediately, no manual save needed.
// True see-through mode has no wallpaper backdrop to rebuild, so this just applies the
// size and repaints. The opacity/accent tint is drawn straight into PaintAll.
static void SetApplyLive(bool sizeChanged, bool preview = false){
    if (!g_hwnd) return;
    if (sizeChanged){
        SetWindowPos(g_hwnd, NULL, 0, 0, cfg.width, cfg.height, SWP_NOMOVE | SWP_NOZORDER);
        KeepOnScreen();
    }
    Repaint();
    if (g_setHwnd) RepaintSet();
}

static void SettingsPickFolder(){
    std::wstring f;
    if (PickFolder(g_setHwnd, f)){
        cfg.folder = f;
        SaveConfig();
        Reload();
        if (g_setHwnd) RepaintSet();
    }
}

// picking a colour just applies it immediately; opacity stays exactly where the user
// set it (the slider is now a linear glass-opacity control, so 0 really is see-through)
static void ApplyAccent(){
    SaveConfig();
    SetApplyLive(false);
}

// apply a user-typed hex string like "#FF0066" (or "FF0066"); returns true if valid
static bool ApplyHex(const std::wstring& s){
    std::wstring v = s;
    if (!v.empty() && v[0] == L'#') v = v.substr(1);
    if (v.size() != 6) return false;
    for (wchar_t c : v){
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')))
            return false;
    }
    COLORREF c = ParseHex(v, cfg.accent);
    cfg.accent = c;
    ApplyAccent();
    return true;
}

static void SettingsPickColor(){
    static COLORREF custom[16] = {0};
    CHOOSECOLORW cc = {0};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g_setHwnd;
    cc.rgbResult = cfg.accent;
    cc.lpCustColors = custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (ChooseColorW(&cc)){
        cfg.accent = cc.rgbResult;
        ApplyAccent();
    }
}

static void SettingsReset(){
    cfg = Config();
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, buf) == S_OK) cfg.folder = buf;
    SaveConfig();
    SitOnDesktop();
    ApplyAutoStart();
    SetWindowPos(g_hwnd, NULL, 0, 0, cfg.width, cfg.height, SWP_NOMOVE | SWP_NOZORDER);
    Reload();
    Repaint();
    if (g_setHwnd) RepaintSet();
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch (msg){
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            // float above apps while it has focus, behave like a normal window afterwards
            if (LOWORD(wParam) == WA_INACTIVE)
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        case WM_PAINT:
            // layered window: repaint via the ULW surface, not GDI. (WM_PAINT rarely fires,
            // but if it does, just re-submit; ValidateRect to consume the update region.)
            RepaintSet();
            {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hit = SetHit(p);
            if (hit == SH_CLOSE){ DestroyWindow(hwnd); return 0; }
            // clicking anything but the hex box dismisses the hex editor
            if (hit != SH_HEX && g_hexEditing){
                g_hexEditing = false;
                RepaintSet();
            }
            if (hit >= SH_SLIDER0 && hit <= SH_SLIDER2){
                g_setActive = hit;
                SetCapture(hwnd);
                SetSliderFromX(hit, p.x);
                SetApplyLive(hit == SH_SLIDER0 || hit == SH_SLIDER1);
                return 0;
            }
            if (hit == SH_FOLDER){ SettingsPickFolder(); return 0; }
            if (hit >= SH_P0 && hit <= SH_P5){
                cfg.accent = g_presets[hit - SH_P0];
                ApplyAccent();
                return 0;
            }
            if (hit == SH_CUSTOM){ SettingsPickColor(); return 0; }
            if (hit == SH_HEX){
                g_hexEditing = true;
                g_hexBuf = L"#" + HexColor(cfg.accent);
                RepaintSet();
                return 0;
            }
            if (hit == SH_AUTO){
                cfg.autostart = !cfg.autostart;
                ApplyAutoStart();
                SaveConfig();
                RepaintSet();
                return 0;
            }
            if (hit >= SH_HZ0 && hit <= SH_HZ5){
                cfg.refreshHz = g_hzPresets[hit - SH_HZ0];
                g_liveBgInterval = (DWORD)(1000.0 / cfg.refreshHz + 0.5);
                if (g_liveBgInterval < 4) g_liveBgInterval = 4;
                if (g_liveBgInterval > 33) g_liveBgInterval = 33;
                SaveConfig();
                RepaintSet();
                return 0;
            }
            if (hit == SH_RESET){ SettingsReset(); return 0; }
            if (hit == SH_DONE){ SaveConfig(); DestroyWindow(hwnd); return 0; }

            // drag the panel by its title bar
            if (p.y < S(52)){
                g_setDragTitle = true;
                POINT sp = p;
                ClientToScreen(hwnd, &sp);
                RECT wr; GetWindowRect(hwnd, &wr);
                g_setDragOff.x = sp.x - wr.left;
                g_setDragOff.y = sp.y - wr.top;
                SetCapture(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (g_setActive >= SH_SLIDER0 && g_setActive <= SH_SLIDER2){
                // Slider drag: live onChange — update the thumb AND the widget preview
                // on every mouse-move, "slide to where, change to where". The panel is a
                // true-see-through layered window now (no screen capture), so this is a
                // cheap in-memory repaint and stays smooth.
                SetSliderFromX(g_setActive, p.x);
                SetApplyLive(g_setActive == SH_SLIDER0 || g_setActive == SH_SLIDER1, true);
                RepaintSet();
                return 0;
            }
            if (g_setDragTitle){
                POINT sp = p;
                ClientToScreen(hwnd, &sp);
                // just move the window; the see-through backdrop is DWM-composited live,
                // so nothing needs re-capturing or repainting on move (no flicker).
                SetWindowPos(hwnd, NULL, sp.x - g_setDragOff.x, sp.y - g_setDragOff.y,
                             0, 0, SWP_NOSIZE | SWP_NOZORDER);
                return 0;
            }
            int hit = SetHit(p);
            if (hit != g_setHover){
                g_setHover = hit;
                RepaintSet();
            }
            bool hand = (hit == SH_CLOSE || hit == SH_FOLDER || hit == SH_CUSTOM ||
                         hit == SH_AUTO || hit == SH_RESET || hit == SH_DONE ||
                         (hit >= SH_P0 && hit <= SH_P5) ||
                         (hit >= SH_HZ0 && hit <= SH_HZ5) ||
                         (hit >= SH_SLIDER0 && hit <= SH_SLIDER2));
            if (hit == SH_HEX) SetCursor(LoadCursorW(NULL, IDC_IBEAM));
            else SetCursor(LoadCursorW(NULL, hand ? IDC_HAND : IDC_ARROW));
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (g_setActive != SH_NONE){
                // position was already updated live during the drag; on release just do
                // the full-quality apply (width/height settle the widget) and save.
                g_setActive = SH_NONE;
                ReleaseCapture();
                SaveConfig();
                SetApplyLive(false, false);
                RepaintSet();
            }
            if (g_setDragTitle){
                g_setDragTitle = false;
                ReleaseCapture();
                // the backdrop is live-composited; nothing to refresh on release
            }
            return 0;
        }
        case WM_CHAR:
            if (g_hexEditing){
                wchar_t ch = (wchar_t)wParam;
                if (ch == L'\r' || ch == L'\n' || ch == L'\x1b'){
                    // enter / escape: commit or cancel
                    if (ch == L'\r') ApplyHex(g_hexBuf);
                    g_hexEditing = false;
                    RepaintSet();
                    return 0;
                }
                if (ch == L'\b'){
                    if (g_hexBuf.size() > 1) g_hexBuf.pop_back();
                    RepaintSet();
                    return 0;
                }
                if (ch == L'#'){
                    if (g_hexBuf.empty()){ g_hexBuf += L'#'; RepaintSet(); }
                    return 0;
                }
                bool hexc = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F');
                if (hexc){
                    // strip a leading '#' when counting hex digits
                    int digits = (int)g_hexBuf.size() - ((!g_hexBuf.empty() && g_hexBuf[0] == L'#') ? 1 : 0);
                    if (digits < 6){
                        wchar_t up = (ch >= L'a' && ch <= L'f') ? (wchar_t)(ch - L'a' + L'A') : ch;
                        if (g_hexBuf.empty()) g_hexBuf += L'#';
                        g_hexBuf += up;
                        if ((int)g_hexBuf.size() - 1 == 6) ApplyHex(g_hexBuf);  // full colour typed -> live
                        RepaintSet();
                    }
                    return 0;
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (g_setHover != SH_NONE){
                g_setHover = SH_NONE;
                RepaintSet();
            }
            return 0;
        case WM_CLOSE:
            SaveConfig();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_setHwnd = NULL;
            FreeSetLayeredSurface();
            // settings window is gone: repaint the widget
            if (g_hwnd) Repaint();
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void OpenSettings(){
    if (g_setHwnd){
        SetWindowPos(g_setHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ShowWindow(g_setHwnd, SW_SHOW);
        SetForegroundWindow(g_setHwnd);
        return;
    }

    int w = S(SET_W), h = S(SET_H);
    // Center the settings panel on the screen's work area (not over the widget): both
    // are translucent, and stacking them made the two see-through layers hard to tell
    // apart. Screen-centered keeps them clearly separated.
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - w) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    if (x + w > wa.right) x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;

    g_setHwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, L"GlassDockSettings", L"GlassDock 设置",
                                WS_POPUP, x, y, w, h, g_hwnd, NULL, g_hInst, NULL);
    if (!g_setHwnd) return;

    RoundCorners(g_setHwnd);
    ShowWindow(g_setHwnd, SW_SHOW);
    RepaintSet();
    BringWindowToTop(g_setHwnd);
    SetForegroundWindow(g_setHwnd);
}

// ---------------- input ----------------
static bool g_dragging = false;
static bool g_resizing = false;
static POINT g_dragOff = {0, 0};
static SIZE g_resizeStart = {0, 0};
static POINT g_resizeStartPos = {0, 0};  // window top-left when the resize began
static POINT g_resizePt = {0, 0};        // pointer SCREEN position when the resize began
static int g_pressedBtn = -1;

// which edge/corner the pointer is over (for cursor + drag-resize)
enum ResizeMode { RS_NONE = 0, RS_E, RS_W, RS_S, RS_N, RS_NE, RS_NW, RS_SE, RS_SW };
static ResizeMode g_resizeMode = RS_NONE;

// hit-test the window border/corners for resize, returning the mode
// ONLY the right edge, the bottom edge and the bottom-right corner are grabbable.
//
// The left and top edges (and any corner touching them) are deliberately disabled:
// resizing from those sides has to move the window's top-left origin, and a
// per-pixel-alpha layered window whose origin moves under the pointer leaves a
// ghost trail and judders. Right/bottom resizing only changes the SIZE — the origin
// never moves — so the drag stays perfectly stable.
static ResizeMode HitResize(POINT p, int w, int h){
    const int edge = S(6);     // border grab thickness
    bool right  = p.x >= w - edge;
    bool bottom = p.y >= h - edge;
    if (right && bottom) return RS_SE;
    if (right)  return RS_E;
    if (bottom) return RS_S;
    return RS_NONE;
}

static LPWSTR CursorForMode(ResizeMode m){
    switch (m){
        case RS_E: case RS_W: return IDC_SIZEWE;
        case RS_N: case RS_S: return IDC_SIZENS;
        case RS_NE: case RS_SW: return IDC_SIZENESW;
        case RS_NW: case RS_SE: return IDC_SIZENWSE;
        default: return IDC_ARROW;
    }
}

static int HitRow(POINT p, int w, int h){
    if (p.y < TitleH() || p.y >= h) return -1;
    int i = (p.y - TitleH() + g_scroll) / RowH();
    if (i >= 0 && i < (int)g_rows.size()) return i;
    return -1;
}

static int HitButton(POINT p, int w){
    RECT b[3]; GetButtons(w, b);
    for (int i = 0; i < 2; i++) if (InRect(b[i], p)) return i;
    return -1;
}

static void SetCursors(POINT p, int w, int h){
    ResizeMode rm = HitResize(p, w, h);
    if (rm != RS_NONE){ SetCursor(LoadCursorW(NULL, CursorForMode(rm))); return; }
    if (HitButton(p, w) >= 0){ SetCursor(LoadCursorW(NULL, IDC_HAND)); return; }
    int row = HitRow(p, w, h);
    if (row >= 0 && g_rows[row].item->isDir){
        int x = LeftPad() + g_rows[row].depth * Indent();
        int y = TitleH() - g_scroll + row * RowH();
        if (p.x >= x && p.x < x + S(16)){ SetCursor(LoadCursorW(NULL, IDC_HAND)); return; }
    }
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
}

// ---------------- context menu ----------------
enum { MENU_PICK = 2001, MENU_SETTINGS, MENU_REFRESH, MENU_AUTO, MENU_QUIT, MENU_SHOW, MENU_HIDE };

static void ShowContextMenu(POINT pt){
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, MENU_PICK, L"选择文件夹…");
    AppendMenuW(menu, MF_STRING, MENU_SETTINGS, L"设置");
    AppendMenuW(menu, MF_STRING, MENU_REFRESH, L"刷新背景");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (cfg.autostart ? MF_CHECKED : 0), MENU_AUTO, L"开机自启");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_HIDE, L"隐藏到托盘");
    AppendMenuW(menu, MF_STRING, MENU_QUIT, L"退出");
    SetNoActivate(false);
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(menu);
    SetNoActivate(true);
    SitOnDesktop();
    if (cmd) SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)cmd, 0);
}

// ---------------- tray icon / global hotkey ----------------
#define WM_TRAYICON (WM_APP + 1)
#define WM_SHOWME   (WM_APP + 99)
#define TIMER_BG    1
#define TIMER_GUARD 3
static bool g_userHidden = false;   // user closed it on purpose -> guard keeps hands off
static int  g_guardTick = 0;
static bool g_trayAdded = false;
static bool g_balloonShown = false;
static const UINT HOTKEY_TOGGLE = 1;

static void AddTrayIcon(){
    if (g_trayAdded) return;
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    lstrcpynW(nid.szTip, L"GlassDock 桌面小组件（Ctrl+Alt+G 显示 / 隐藏）", 128);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) g_trayAdded = true;
}

static void RemoveTrayIcon(){
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

static void ShowWidget(){
    RenderBackground();
    BOOL wasVis = IsWindowVisible(g_hwnd);
    g_userHidden = false;
    SitOnDesktop();   // shows + re-anchors above the wallpaper in one atomic call
    if (!wasVis) Repaint();
}

static void HideWidget(){
    g_userHidden = true;
    ShowWindow(g_hwnd, SW_HIDE);
}

// "Show on demand" — used by double-click on the tray/desktop icon and the hotkey.
// This is deliberately NOT a toggle: the widget is meant to live permanently on the
// desktop (like a desktop icon), so these entry points simply make sure it is shown
// and re-anchored to the desktop layer. A toggle based on IsWindowVisible is unreliable
// here because a widget buried below the wallpaper still reports IsWindowVisible==TRUE,
// which made a double-click hide it instead of showing it.
static void ToggleWidget(){
    ShowWidget();
}

static void NotifyHidden(){
    if (g_balloonShown || !g_trayAdded) return;
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    lstrcpynW(nid.szInfoTitle, L"GlassDock", 64);
    lstrcpynW(nid.szInfo, L"已隐藏到系统托盘。\n双击托盘图标，或按 Ctrl+Alt+G 即可呼出。", 256);
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    g_balloonShown = true;
}

static void ShowTrayMenu(){
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, MENU_SHOW, IsWindowVisible(g_hwnd) ? L"隐藏小组件" : L"显示小组件");
    AppendMenuW(menu, MF_STRING, MENU_SETTINGS, L"打开设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_QUIT, L"退出");
    SetNoActivate(false);
    SetForegroundWindow(g_hwnd);
    POINT pt; GetCursorPos(&pt);
    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(menu);
    SetNoActivate(true);
    SitOnDesktop();
    if (cmd) SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)cmd, 0);
}

// ---------------- window proc ----------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch (msg){
        case WM_ERASEBKGND:
            return 1;
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) ToggleWidget();
            else if (lParam == WM_RBUTTONUP) ShowTrayMenu();
            return 0;
        case WM_SHOWME:
            ShowWidget();
            return 0;
        case WM_HOTKEY:
            if (wParam == HOTKEY_TOGGLE) ToggleWidget();
            return 0;
        case WM_SETTINGCHANGE:
            // wallpaper / theme may have changed -> refresh the captured background
            RenderBackground();
            return 0;
        case WM_ACTIVATE:
            // it never takes focus (WS_EX_NOACTIVATE); if something still tries, stay put
            return 0;
        case WM_SIZE:
            // a desktop gadget must stay put; if anything tries to minimize us, restore
            if (wParam == SIZE_MINIMIZED){
                ShowWindow(hwnd, SW_SHOW);
                SitOnDesktop();
                Repaint();
            }
            return 0;
        case WM_TIMER:
            if (wParam == TIMER_BG){
                // In true see-through mode there is no wallpaper snapshot to refresh;
                // this timer is now a no-op (kept so a future frosted-glass mode can
                // hook back in). The guard timer below still keeps us anchored.
            } else if (wParam == TIMER_GUARD){
                // Keep the gadget glued to the desktop. Re-show it if anything hid or
                // minimized it, and periodically re-anchor it above Progman so it never
                // gets buried below the wallpaper by a stray z-order change.
                if (!g_userHidden){
                    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)){
                        SitOnDesktop();
                        Repaint();
                    }
                    if (++g_guardTick >= 5){
                        g_guardTick = 0;
                        if (!g_dragging && !g_resizing) SitOnDesktop();
                    }
                }
            }
            return 0;
        case WM_NCHITTEST:
            // The whole window is one solid hit target. On a per-pixel-alpha layered
            // window, pixels with alpha=0 would otherwise report HTTRANSPARENT and let
            // the click fall through to whatever is behind (e.g. a desktop icon the
            // widget is parked on top of). Force the entire rectangle to hit the client
            // area so the gadget behaves as a single solid object.
            return HTCLIENT;
        case WM_PAINT: {
            // Per-pixel alpha: paint into the layered surface and submit it, instead of
            // the old GDI double-buffer + BitBlt (which cannot express per-pixel alpha).
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            CommitLayered();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT r; GetClientRect(hwnd, &r);
            int w = r.right, h = r.bottom;
            // resize edge/corner first
            ResizeMode rm = HitResize(p, w, h);
            if (rm != RS_NONE){
                g_resizing = true;
                g_resizeMode = rm;
                g_resizeStart.cx = w; g_resizeStart.cy = h;
                RECT wr; GetWindowRect(hwnd, &wr);
                g_resizeStartPos.x = wr.left; g_resizeStartPos.y = wr.top;
                // Record the pointer in SCREEN coordinates. During a left/right-edge
                // resize the window itself is moved by UpdateLayeredWindow, which shifts
                // the client origin — so a client-space delta would be wrong (it makes the
                // width jitter/ghost). Screen-space is absolute and stays correct.
                POINT sp0 = {p.x, p.y}; ClientToScreen(hwnd, &sp0);
                g_resizePt = sp0; SetCapture(hwnd);
                return 0;
            }
            int btn = HitButton(p, w);
            if (btn >= 0){ g_pressedBtn = btn; SetCapture(hwnd); return 0; }
            if (p.y < TitleH()){
                g_dragging = true;
                POINT sp = {p.x, p.y};
                ClientToScreen(hwnd, &sp);
                RECT wr; GetWindowRect(hwnd, &wr);
                g_dragOff.x = sp.x - wr.left; g_dragOff.y = sp.y - wr.top;
                SetCapture(hwnd);
                return 0;
            }
            int row = HitRow(p, w, h);
            if (row >= 0 && g_rows[row].item->isDir){
                int x = LeftPad() + g_rows[row].depth * Indent();
                int y = TitleH() - g_scroll + row * RowH();
                if (p.x >= x && p.x < x + S(16) && p.y >= y && p.y < y + RowH()){
                    Toggle(g_rows[row].item);
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT r; GetClientRect(hwnd, &r);
            int w = r.right, h = r.bottom;
            if (g_dragging){
                POINT sp = {p.x, p.y}; ClientToScreen(hwnd, &sp);
                SetWindowPos(hwnd, NULL, sp.x - g_dragOff.x, sp.y - g_dragOff.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                // True see-through mode: the background is fully transparent, so nothing
                // to re-render while dragging — just re-submit the layered surface.
                Repaint();
                return 0;
            }
            if (g_resizing){
                int nw = g_resizeStart.cx, nh = g_resizeStart.cy;
                int nx = g_resizeStartPos.x, ny = g_resizeStartPos.y;
                // delta in SCREEN space (see WM_LBUTTONDOWN). The client-space pointer
                // would drift as the window is moved during an edge resize, producing
                // the width "jitter / ghosting" on fast left/right drags.
                POINT sp = {p.x, p.y}; ClientToScreen(hwnd, &sp);
                int dx = sp.x - g_resizePt.x, dy = sp.y - g_resizePt.y;
                switch (g_resizeMode){
                    case RS_E:  nw = g_resizeStart.cx + dx; break;
                    case RS_S:  nh = g_resizeStart.cy + dy; break;
                    case RS_SE: nw = g_resizeStart.cx + dx; nh = g_resizeStart.cy + dy; break;
                    default: break;
                }
                if (nw < S(200)) nw = S(200);
                if (nh < S(160)) nh = S(160);
                // nx/ny stay at the window origin on purpose: only the right/bottom
                // edges are draggable, so resizing never moves the window itself.
                RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
                if (nw > wa.right) nw = wa.right;
                if (nh > wa.bottom) nh = wa.bottom;
                cfg.width = nw; cfg.height = nh;
                // Do NOT SetWindowPos here. A layered window's rect is owned by
                // UpdateLayeredWindow; changing the size via SetWindowPos and then
                // re-submitting in the same tick lets DWM splice the old frame against the
                // new size (the double-image ghost). Instead we hand the target position to
                // CommitLayered, which moves+sizes the window atomically via
                // UpdateLayeredWindow's pptDst/psize.
                g_resizeHasPos = true;
                g_resizePos.x = nx; g_resizePos.y = ny;
                Repaint();
                return 0;
            }
            // hover
            int prevRow = g_hoverRow, prevBtn = g_hoverBtn;
            g_hoverRow = HitRow(p, w, h);
            g_hoverBtn = HitButton(p, w);
            if (prevRow != g_hoverRow || prevBtn != g_hoverBtn) Repaint();
            SetCursors(p, w, h);
            // mouse leave tracking
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_hoverRow != -1 || g_hoverBtn != -1){
                g_hoverRow = -1; g_hoverBtn = -1;
                Repaint();
            }
            return 0;
        case WM_LBUTTONUP: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT r; GetClientRect(hwnd, &r);
            int w = r.right;
            if (g_dragging || g_resizing){
                g_dragging = g_resizing = false;
                g_resizeMode = RS_NONE;
                g_resizeHasPos = false;
                ReleaseCapture();
                SavePos();
                RenderBackground();
                Repaint();
                return 0;
            }
            if (g_pressedBtn >= 0){
                ReleaseCapture();
                int btn = g_pressedBtn;
                g_pressedBtn = -1;
                if (HitButton(p, w) == btn){
                    switch (btn){
                        case 0: Reload(); RenderBackground(); break;
                        case 1: OpenSettings(); break;
                        case 2: HideWidget(); NotifyHidden(); break;
                    }
                }
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT r; GetClientRect(hwnd, &r);
            int row = HitRow(p, r.right, r.bottom);
            if (row >= 0){
                Item* it = g_rows[row].item;
                // if dir and clicked on arrow, toggle; else open
                if (it->isDir){
                    int x = LeftPad() + g_rows[row].depth * Indent();
                    int y = TitleH() - g_scroll + row * RowH();
                    if (p.x >= x && p.x < x + S(16) && p.y >= y && p.y < y + RowH()){ Toggle(it); return 0; }
                }
                OpenItem(it);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT r; GetClientRect(hwnd, &r);
            g_scroll -= delta / WHEEL_DELTA * S(60);
            ClampScroll(r.right, r.bottom);
            Repaint();
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT sp = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &sp);
            ShowContextMenu(sp);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)){
                case MENU_PICK: {
                    std::wstring f;
                    if (PickFolder(hwnd, f)){ cfg.folder = f; SaveConfig(); Reload(); }
                    break;
                }
                case MENU_SETTINGS: OpenSettings(); break;
                case MENU_REFRESH: RenderBackground(); Repaint(); break;
                case MENU_AUTO: cfg.autostart = !cfg.autostart; SaveConfig(); ApplyAutoStart(); break;
                case MENU_SHOW: ToggleWidget(); break;
                case MENU_HIDE: HideWidget(); NotifyHidden(); break;
                case MENU_QUIT: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            RemoveTrayIcon();
            UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
            SaveConfig();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------- winmain ----------------
static void RunUninstall(){
    wchar_t desktop[MAX_PATH] = {0}, programs[MAX_PATH] = {0}, appdata[MAX_PATH] = {0};
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop);
    SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, programs);
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata);

    wchar_t lnk[MAX_PATH];
    if (desktop[0]){
        wsprintfW(lnk, L"%s\\GlassDock.lnk", desktop);
        DeleteFileW(lnk);
    }
    if (programs[0]){
        wsprintfW(lnk, L"%s\\GlassDock.lnk", programs);
        DeleteFileW(lnk);
    }

    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) == ERROR_SUCCESS){
        RegDeleteValueW(k, L"GlassDock");
        RegCloseKey(k);
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GlassDock");

    MessageBoxW(NULL, L"GlassDock 已从系统中卸载。", L"GlassDock 卸载", MB_OK | MB_ICONINFORMATION);

    // schedule cleanup of the install dir + config dir after this process exits
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t dir[MAX_PATH]; wcscpy_s(dir, exe);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;

    wchar_t cfgdir[MAX_PATH] = {0};
    if (appdata[0]) wsprintfW(cfgdir, L"%s\\GlassDock", appdata);

    wchar_t cmd[2048];
    wsprintfW(cmd, L"/c ping -n 2 127.0.0.1 >nul & rd /s /q \"%s\" & rd /s /q \"%s\"", dir, cfgdir);
    ShellExecuteW(NULL, L"open", L"cmd.exe", cmd, NULL, SW_HIDE);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int){
    g_hInst = hInstance;
    SetProcessDPIAware();

    if (wcsstr(GetCommandLineW(), L"--uninstall")){
        RunUninstall();
        return 0;
    }

    LoadConfig();
    // live drag rebuild interval from the chosen refresh rate (Hz -> ms), clamped so a
    // very low rate still feels responsive and a very high one doesn't peg the CPU
    g_liveBgInterval = (DWORD)(1000.0 / cfg.refreshHz + 0.5);
    if (g_liveBgInterval < 4) g_liveBgInterval = 4;    // cap at ~240 Hz
    if (g_liveBgInterval > 33) g_liveBgInterval = 33;  // floor at ~30 Hz

    // scale
    HDC screen = GetDC(NULL);
    g_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    if (g_scale < 1.0f) g_scale = 1.0f;
    ReleaseDC(NULL, screen);

    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdiplusToken, &gsi, NULL);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    // single instance: launching again just brings the existing widget back
    HANDLE hSingle = CreateMutexW(NULL, FALSE, L"Local\\GlassDock_SingleInstance");
    if (hSingle && GetLastError() == ERROR_ALREADY_EXISTS){
        HWND other = FindWindowW(L"GlassDockWidget", NULL);
        if (other) PostMessageW(other, WM_SHOWME, 0, 0);
        return 0;
    }

    InitFonts();
    LoadRoot();

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GlassDockWidget";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DBLCLKS;
    RegisterClassW(&wc);

    WNDCLASSW sc = {0};
    sc.lpfnWndProc = SettingsProc;
    sc.hInstance = hInstance;
    sc.lpszClassName = L"GlassDockSettings";
    sc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    sc.hbrBackground = NULL;
    RegisterClassW(&sc);

    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = cfg.x, y = cfg.y;
    if (x < 0 || y < 0){
        x = wa.right - cfg.width - S(24);
        y = wa.top + S(60);
    }

    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                             L"GlassDockWidget", L"GlassDock",
                             WS_POPUP, x, y, cfg.width, cfg.height,
                             NULL, NULL, hInstance, NULL);
    if (!g_hwnd) return 1;

    RoundCorners(g_hwnd);
    ApplyAutoStart();
    // register against the window itself; RegisterHotKey(NULL) posts to the thread queue
    RegisterHotKey(g_hwnd, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT, 'G');
    RenderBackground();
    AddTrayIcon();
    SetTimer(g_hwnd, TIMER_BG, 900, NULL);
    SetTimer(g_hwnd, TIMER_GUARD, 1000, NULL);

    ShowWindow(g_hwnd, SW_SHOW);
    SitOnDesktop();
    Repaint();          // first layered submit while the window is visible
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0){
        if (msg.message == WM_HOTKEY && msg.hwnd == NULL){
            if (msg.wParam == HOTKEY_TOGGLE) ToggleWidget();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(g_hwnd, TIMER_BG);
    KillTimer(g_hwnd, TIMER_GUARD);
    FreeIcons();
    delete g_root;
    delete g_bg;
    delete g_screenShot;
    delete g_screenShotSmall;
    if (g_bbMain.dc){ if (g_bbMain.bmp) SelectObject(g_bbMain.dc, g_bbMain.old); DeleteDC(g_bbMain.dc); }
    if (g_bbMain.bmp) DeleteObject(g_bbMain.bmp);
    if (g_bbSet.dc){ if (g_bbSet.bmp) SelectObject(g_bbSet.dc, g_bbSet.old); DeleteDC(g_bbSet.dc); }
    if (g_bbSet.bmp) DeleteObject(g_bbSet.bmp);
    FreeLayeredSurface();
    FreeSetLayeredSurface();
    GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    return (int)msg.wParam;
}
