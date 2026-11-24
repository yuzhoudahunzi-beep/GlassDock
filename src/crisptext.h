// crisptext.h — GDI-backed crisp text rendering for the per-pixel-alpha widget.
//
// WHY
// ---
// GDI+ rasterises glyphs with its own soft grayscale anti-aliaser.  At 12-15 px a
// CJK stem is thinner than one device pixel, so GDI+ splits that stem over two
// half-covered grey pixels.  Measured on the widget's own list text:
//
//     GDI+ AAGridFit @15px      solid stroke pixels   6.8 %   edge 1.98 px
//     GDI  grayscale AA @15px    solid stroke pixels  54.7 %   edge 1.08 px
//
// 54.7 % of the ink being solid white (instead of 6.8 %) is exactly the
// difference between "washed out / 发虚" and "as sharp as a native Windows label".
// Explorer draws the desktop icon labels with GDI ClearType; we cannot use
// ClearType here because the widget is a per-pixel-alpha layered window over an
// arbitrary wallpaper (sub-pixel fringes would be colour garbage), but the
// grayscale GDI path gets us the same perceived sharpness.
//
// HOW
// ---
//   1. measure the string with GDI (so ellipsis + wrapping behave like Windows)
//   2. rasterise it with DrawTextW into a small 32 bpp scratch DIB, black on black
//   3. the R channel of that DIB is the per-pixel coverage of the glyph
//   4. blend the coverage into the PARGB target as premultiplied text
//
// Everything is done with raw GDI + direct pixel writes.  GDI+'s DrawImage(Rect)
// overload is unusable here: MinGW declares GdipDrawImageRectRectI with four
// separate ints where the real API takes a GpRect*, and Graphics::Clear() does
// not actually produce a transparent PARGB surface.
#ifndef GLASSDOCK_CRIPTTEXT_H
#define GLASSDOCK_CRIPTTEXT_H

#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <cwchar>

namespace crisp {

// ---- alignment (mirrors GDI+ StringAlignment) --------------------------
enum HAlign { HLeft = 0, HCenter = 1, HRight = 2 };
enum VAlign { VTop = 0, VMiddle = 1, VBottom = 2 };

// ---- scratch surface used for the GDI rasterisation --------------------
struct Scratch {
    HBITMAP bmp; HDC dc; BYTE* bits; int w; int h;
    Scratch() : bmp(NULL), dc(NULL), bits(NULL), w(0), h(0) {}
};
static Scratch g_scratch;

static void FreeScratch(){
    if (g_scratch.dc){ DeleteDC(g_scratch.dc); g_scratch.dc = NULL; }
    if (g_scratch.bmp){ DeleteObject(g_scratch.bmp); g_scratch.bmp = NULL; }
    g_scratch.bits = NULL; g_scratch.w = g_scratch.h = 0;
}

static bool EnsureScratch(int w, int h){
    if (w <= 0 || h <= 0) return false;
    if (g_scratch.bmp && g_scratch.w >= w && g_scratch.h >= h) return true;
    FreeScratch();
    HDC screen = GetDC(NULL);
    if (!screen) return false;
    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth  = w;
    bi.bmiHeader.biHeight = -h;                 // top-down
    bi.bmiHeader.biPlanes  = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!dib || !dc){
        if (dib) DeleteObject(dib);
        if (dc)  DeleteDC(dc);
        return false;
    }
    SelectObject(dc, dib);
    g_scratch.bmp  = dib;
    g_scratch.dc   = dc;
    g_scratch.bits = (BYTE*)bits;
    g_scratch.w    = w;
    g_scratch.h    = h;
    return true;
}

// ---- HFONT factory ----------------------------------------------------
inline HFONT MakeHFont(const wchar_t* family, int px, bool bold, bool italic = false){
    if (!family || !*family) family = L"Segoe UI";
    if (px < 6) px = 6;
    LOGFONTW lf; ZeroMemory(&lf, sizeof lf);
    lf.lfHeight = -px;
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = italic ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = ANTIALIASED_QUALITY;          // grayscale AA, hinted
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_DONTCARE;
    wcsncpy_s(lf.lfFaceName, family, LF_FACESIZE - 1);
    return CreateFontIndirectW(&lf);
}

// ---- rasterise a string into a coverage mask ----------------------------
// `boxW`/`boxH` are the size of the layout box in device pixels, the alignment
// flags place the run inside that box, and the returned mask is the *ink*
// rectangle only.  outOX/outOY is the offset of the mask inside the box.
//
// The mask holds one byte per pixel: 0 = no ink, 255 = fully covered.
inline bool Rasterize(HFONT hf, const std::wstring& text,
                       int boxW, int boxH, int hAlign, int vAlign, bool ellipsis,
                       std::vector<BYTE>& cov, int& outW, int& outH,
                       int& outOX, int& outOY){
    cov.clear(); outW = outH = outOX = outOY = 0;
    if (!hf || text.empty() || boxW <= 0 || boxH <= 0) return false;

    // the scratch has to hold the *whole* layout box so that the alignment
    // offsets can be computed inside it
    if (!EnsureScratch(boxW, boxH)) return false;

    HGDIOBJ oldFont = SelectObject(g_scratch.dc, hf);
    SetBkMode(g_scratch.dc, OPAQUE);
    SetBkColor(g_scratch.dc, RGB(0, 0, 0));
    SetTextColor(g_scratch.dc, RGB(255, 255, 255));

    UINT base = DT_SINGLELINE | DT_NOPREFIX | DT_LEFT | DT_TOP;
    if (ellipsis) base |= DT_END_ELLIPSIS;

    // pass 1 – measure exactly the way Windows would lay the run out
    RECT mr; mr.left = 0; mr.top = 0; mr.right = boxW; mr.bottom = boxH;
    DrawTextW(g_scratch.dc, text.c_str(), -1, &mr, base | DT_CALCRECT);
    int tw = mr.right - mr.left;
    int th = mr.bottom - mr.top;
    if (tw <= 0 || th <= 0){ SelectObject(g_scratch.dc, oldFont); return false; }
    if (tw > boxW) tw = boxW;

    int offX = 0, offY = 0;
    if (hAlign == HCenter)     offX = (boxW - tw) / 2;
    else if (hAlign == HRight) offX = boxW - tw;
    if (vAlign == VMiddle)     offY = (boxH - th) / 2;
    else if (vAlign == VBottom) offY = boxH - th;
    if (offX < 0) offX = 0;
    if (offY < 0) offY = 0;

    // pass 2 – rasterise into a tight scratch of the ink size
    int sw = tw + 2, sh = th + 2;
    if (!EnsureScratch(sw, sh)){ SelectObject(g_scratch.dc, oldFont); return false; }
    for (int y = 0; y < sh; y++)
        memset(g_scratch.bits + (size_t)y * g_scratch.w * 4, 0, (size_t)g_scratch.w * 4);
    HGDIOBJ oldFont2 = SelectObject(g_scratch.dc, hf);
    SetBkMode(g_scratch.dc, OPAQUE);
    SetBkColor(g_scratch.dc, RGB(0, 0, 0));
    SetTextColor(g_scratch.dc, RGB(255, 255, 255));
    RECT dr; dr.left = 1; dr.top = 1; dr.right = 1 + tw; dr.bottom = 1 + th;
    DrawTextW(g_scratch.dc, text.c_str(), -1, &dr, base);
    GdiFlush();
    SelectObject(g_scratch.dc, oldFont2);
    SelectObject(g_scratch.dc, oldFont);

    // pass 3 – harvest the coverage (R channel on black == coverage)
    cov.assign((size_t)tw * th, 0);
    const BYTE* src = g_scratch.bits;
    int stride = g_scratch.w * 4;
    for (int y = 0; y < th; y++){
        const BYTE* row = src + (size_t)(y + 1) * stride + 4;   // +1 px border
        BYTE* dst = &cov[(size_t)y * tw];
        for (int x = 0; x < tw; x++) dst[x] = row[(size_t)x * 4 + 2];  // R
    }
    outW = tw; outH = th; outOX = offX; outOY = offY;
    return true;
}

// ---- blend a coverage mask into a PARGB bitmap (source-over) -----------
inline void Composite(Gdiplus::Bitmap* bmp, int ox, int oy,
                      const std::vector<BYTE>& cov, int w, int h,
                      BYTE r, BYTE g, BYTE b){
    if (!bmp || w <= 0 || h <= 0) return;
    int bw = (int)bmp->GetWidth(), bh = (int)bmp->GetHeight();
    // clip against the bitmap
    if (ox < 0){ w += ox; ox = 0; }
    if (oy < 0){ h += oy; oy = 0; }
    if (ox + w > bw) w = bw - ox;
    if (oy + h > bh) h = bh - oy;
    if (w <= 0 || h <= 0) return;

    Gdiplus::BitmapData bd;
    Gdiplus::Rect rc(0, 0, bw, bh);
    // (ImageLockMode)3 == ReadWrite; MinGW's enum only exposes Read/Write.
    // PixelFormat32bppPARGB is a macro, so it must not be namespace-qualified.
    if (bmp->LockBits(&rc, (Gdiplus::ImageLockMode)3, PixelFormat32bppPARGB, &bd) != Gdiplus::Ok)
        return;
    BYTE* base = (BYTE*)bd.Scan0;
    int stride = bd.Stride;
    for (int y = 0; y < h; y++){
        BYTE* row = base + (size_t)(oy + y) * stride;
        const BYTE* src = &cov[(size_t)y * w];
        for (int x = 0; x < w; x++){
            BYTE a = src[x];
            if (!a) continue;
            BYTE* p = row + (size_t)(ox + x) * 4;
            int inv = 255 - a;
            int sr = r * a / 255, sg = g * a / 255, sb = b * a / 255;
            p[0] = (BYTE)(sb + p[0] * inv / 255);      // B
            p[1] = (BYTE)(sg + p[1] * inv / 255);      // G
            p[2] = (BYTE)(sr + p[2] * inv / 255);      // R
            p[3] = (BYTE)(a  + p[3] * inv / 255);      // A
        }
    }
    bmp->UnlockBits(&bd);
}

// ---- measure a string the way Windows would lay it out ------------------
// `boxW` is the available width (<=0 means "unbounded"); the returned value is
// the width the run actually occupies (already truncated when ellipsis is on).
inline int MeasureWidth(HFONT hf, const std::wstring& text, int boxW, bool ellipsis){
    if (!hf || text.empty()) return 0;
    if (!EnsureScratch(16, 16)) return 0;      // only the DC matters for DT_CALCRECT
    HGDIOBJ old = SelectObject(g_scratch.dc, hf);
    SetBkMode(g_scratch.dc, OPAQUE);
    SetBkColor(g_scratch.dc, RGB(0, 0, 0));
    SetTextColor(g_scratch.dc, RGB(255, 255, 255));
    UINT base = DT_SINGLELINE | DT_NOPREFIX | DT_LEFT | DT_TOP;
    if (ellipsis) base |= DT_END_ELLIPSIS;
    RECT mr; mr.left = 0; mr.top = 0;
    mr.right  = boxW > 0 ? boxW : 32000;
    mr.bottom = 64;
    DrawTextW(g_scratch.dc, text.c_str(), -1, &mr, base | DT_CALCRECT);
    SelectObject(g_scratch.dc, old);
    int w = mr.right - mr.left;
    if (w < 0) w = 0;
    return w;
}

// ---- one-shot helper: lay out + rasterise + blend ----------------------
// boxX/boxY/boxW/boxH are device pixels; the text is aligned inside that box.
inline bool Draw(Gdiplus::Bitmap* bmp, HFONT hf, const std::wstring& text,
                 int boxX, int boxY, int boxW, int boxH,
                 int hAlign, int vAlign, bool ellipsis,
                 BYTE r, BYTE g, BYTE b){
    std::vector<BYTE> cov;
    int w = 0, h = 0, ox = 0, oy = 0;
    if (!Rasterize(hf, text, boxW, boxH, hAlign, vAlign, ellipsis, cov, w, h, ox, oy))
        return false;
    Composite(bmp, boxX + ox, boxY + oy, cov, w, h, r, g, b);
    return true;
}

// ---- one-shot helper with a drop shadow (desktop-icon-label look) -------
// Rasterises once, then stamps the coverage twice: a dark shadow offset by
// (sox, soy), then the main colour in place.  This mirrors how Explorer draws
// the desktop icon labels — a soft 1px dark shadow under the white glyph so
// the text stays readable on any wallpaper — without ever redrawing the stem
// interior dark (which is what the old 8-way outline got wrong).
inline bool DrawWithShadow(Gdiplus::Bitmap* bmp, HFONT hf, const std::wstring& text,
                 int boxX, int boxY, int boxW, int boxH,
                 int hAlign, int vAlign, bool ellipsis,
                 BYTE r, BYTE g, BYTE b,
                 int sox, int soy, BYTE sr, BYTE sg, BYTE sb, int shadowAlpha){
    std::vector<BYTE> cov;
    int w = 0, h = 0, ox = 0, oy = 0;
    if (!Rasterize(hf, text, boxW, boxH, hAlign, vAlign, ellipsis, cov, w, h, ox, oy))
        return false;
    // shadow first (behind), then the glyph (in front).
    // The shadow uses a DAMPED copy of the coverage so it is a soft translucent
    // fringe, not a solid black duplicate:  shadowCov = cov * shadowAlpha/255.
    // For a dark shadow (sr=sg=sb=0) the colour is unaffected by this scaling.
    if (shadowAlpha > 0){
        std::vector<BYTE> sh = cov;
        for (size_t i = 0; i < sh.size(); i++)
            sh[i] = (BYTE)((int)sh[i] * shadowAlpha / 255);
        Composite(bmp, boxX + ox + sox, boxY + oy + soy, sh, w, h, sr, sg, sb);
    }
    Composite(bmp, boxX + ox, boxY + oy, cov, w, h, r, g, b);
    return true;
}

} // namespace crisp

#endif // GLASSDOCK_CRIPTTEXT_H
