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

    // ---- supersampled rasterisation -------------------------------------
    // Hinted 1x GDI snaps every stem to the pixel grid: at 13-16 px that makes
    // neighbouring CJK glyphs land on different grid phases (one char a pixel
    // taller / fatter than the next) and leaves stair-stepped, "rough" edges.
    // Rendering the run at SS x and box-averaging back down removes the grid
    // snap: every glyph keeps its true outline proportion and the edges become
    // a smooth continuous alpha ramp ("silky"), with no ClearType colour fringe
    // because we still only ever use the grayscale coverage channel.
    LOGFONTW lf; ZeroMemory(&lf, sizeof lf);
    int SS = 3;
    if (GetObjectW(hf, sizeof lf, &lf) != sizeof lf || lf.lfHeight >= 0) SS = 1;
    while (SS > 1 && (boxW * SS > 4096 || boxH * SS > 1536)) SS--;
    HFONT big = NULL;
    if (SS > 1){
        LOGFONTW bl = lf;
        bl.lfHeight = lf.lfHeight * SS;
        bl.lfQuality = ANTIALIASED_QUALITY;
        big = CreateFontIndirectW(&bl);
        if (!big) SS = 1;
    }
    HFONT use = big ? big : hf;

    // pass 1 – measure at SS x (DT_CALCRECT only needs the DC + font)
    if (!EnsureScratch(16, 16)){ if (big) DeleteObject(big); return false; }
    HGDIOBJ oldFont = SelectObject(g_scratch.dc, use);
    SetBkMode(g_scratch.dc, OPAQUE);
    SetBkColor(g_scratch.dc, RGB(0, 0, 0));
    SetTextColor(g_scratch.dc, RGB(255, 255, 255));

    UINT base = DT_SINGLELINE | DT_NOPREFIX | DT_LEFT | DT_TOP;
    if (ellipsis) base |= DT_END_ELLIPSIS;

    RECT mr; mr.left = 0; mr.top = 0; mr.right = boxW * SS; mr.bottom = boxH * SS;
    DrawTextW(g_scratch.dc, text.c_str(), -1, &mr, base | DT_CALCRECT);
    int twB = mr.right - mr.left;
    int thB = mr.bottom - mr.top;
    if (twB <= 0 || thB <= 0){
        SelectObject(g_scratch.dc, oldFont); if (big) DeleteObject(big); return false;
    }
    if (twB > boxW * SS) twB = boxW * SS;

    int offXB = 0, offYB = 0;
    if (hAlign == HCenter)     offXB = (boxW * SS - twB) / 2;
    else if (hAlign == HRight) offXB = boxW * SS - twB;
    if (vAlign == VMiddle)     offYB = (boxH * SS - thB) / 2;
    else if (vAlign == VBottom) offYB = boxH * SS - thB;
    if (offXB < 0) offXB = 0;
    if (offYB < 0) offYB = 0;

    // pass 2 – rasterise the SS x run into a tight scratch of the ink size
    int sw = twB + 2, sh = thB + 2;
    if (!EnsureScratch(sw, sh)){
        SelectObject(g_scratch.dc, oldFont); if (big) DeleteObject(big); return false;
    }
    for (int y = 0; y < sh; y++)
        memset(g_scratch.bits + (size_t)y * g_scratch.w * 4, 0, (size_t)g_scratch.w * 4);
    HGDIOBJ oldFont2 = SelectObject(g_scratch.dc, use);
    SetBkMode(g_scratch.dc, OPAQUE);
    SetBkColor(g_scratch.dc, RGB(0, 0, 0));
    SetTextColor(g_scratch.dc, RGB(255, 255, 255));
    RECT dr; dr.left = 1; dr.top = 1; dr.right = 1 + twB; dr.bottom = 1 + thB;
    DrawTextW(g_scratch.dc, text.c_str(), -1, &dr, base);
    GdiFlush();
    SelectObject(g_scratch.dc, oldFont2);
    SelectObject(g_scratch.dc, oldFont);
    if (big) DeleteObject(big);

    // pass 3 – harvest the SS x coverage (R channel on black == coverage)
    std::vector<BYTE> bigCov((size_t)twB * thB, 0);
    const BYTE* src = g_scratch.bits;
    int stride = g_scratch.w * 4;
    for (int y = 0; y < thB; y++){
        const BYTE* row = src + (size_t)(y + 1) * stride + 4;   // +1 px border
        BYTE* dst = &bigCov[(size_t)y * twB];
        for (int x = 0; x < twB; x++) dst[x] = row[(size_t)x * 4 + 2];  // R
    }

    // pass 4 – box-average SS x SS blocks down to device pixels.  Averaging the
    // hinted big render (instead of hinting at 1x) is what makes the edges smooth
    // and the glyph sizes uniform.
    int tw = (twB + SS / 2) / SS; if (tw < 1) tw = 1;
    int th = (thB + SS / 2) / SS; if (th < 1) th = 1;
    cov.assign((size_t)tw * th, 0);
    for (int y = 0; y < th; y++){
        int y0 = y * SS, y1 = y0 + SS; if (y1 > thB) y1 = thB;
        for (int x = 0; x < tw; x++){
            int x0 = x * SS, x1 = x0 + SS; if (x1 > twB) x1 = twB;
            int sum = 0, n = 0;
            for (int yy = y0; yy < y1; yy++){
                const BYTE* row = &bigCov[(size_t)yy * twB];
                for (int xx = x0; xx < x1; xx++){ sum += row[xx]; n++; }
            }
            cov[(size_t)y * tw + x] = (BYTE)(n ? sum / n : 0);
        }
    }
    outW = tw; outH = th; outOX = offXB / SS; outOY = offYB / SS;
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
