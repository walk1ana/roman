// Windows-бэкенд: окно Win32, рисование Direct2D + DirectWrite (цветные эмодзи), звук/микрофон через winmm.
// Без внешних библиотек: всё входит в Windows 8.1+ / 10 / 11.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dxgiformat.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "platform.h"

#ifdef _MSC_VER
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ------------------------------------------------------------------ глобальные объекты
static HWND g_hwnd = nullptr;
static ID2D1Factory* g_fac = nullptr;
static IDWriteFactory* g_dw = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static ID2D1SolidColorBrush* g_brush = nullptr;
static ID2D1StrokeStyle *g_strokeRound = nullptr, *g_strokeFlat = nullptr;
static std::vector<ID2D1Layer*> g_layers;
static std::vector<Image*> g_uploaded;               // для сброса при потере устройства
static float g_scale = 1, g_offX = 0, g_offY = 0;    // letterbox

// ------------------------------------------------------------------ состояние преобразований
static D2D1_MATRIX_3X2_F mk(float a, float b, float c, float d, float e, float f) { D2D1_MATRIX_3X2_F m; m._11 = a; m._12 = b; m._21 = c; m._22 = d; m._31 = e; m._32 = f; return m; }
static D2D1_MATRIX_3X2_F mm(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) {      // a, затем b (векторы-строки, как в Direct2D)
    return mk(a._11 * b._11 + a._12 * b._21, a._11 * b._12 + a._12 * b._22, a._21 * b._11 + a._22 * b._21, a._21 * b._12 + a._22 * b._22,
              a._31 * b._11 + a._32 * b._21 + b._31, a._31 * b._12 + a._32 * b._22 + b._32);
}
struct ClipEntry { char kind; ID2D1Geometry* geom; };   // 'a' — axis-aligned, 'l' — слой
struct SaveRec { D2D1_MATRIX_3X2_F m; float alpha; size_t clips; };
static D2D1_MATRIX_3X2_F g_cm, g_base;
static float g_alpha = 1.f;
static std::vector<SaveRec> g_stack;
static std::vector<ClipEntry> g_clips;
static int g_layerDepth = 0;

static void applyTransform() { D2D1_MATRIX_3X2_F t = mm(g_cm, g_base); g_rt->SetTransform(t); }

static std::wstring toW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w;
}
static D2D1_COLOR_F col(Color c, float extra = 1.f) { return D2D1::ColorF(c.r, c.g, c.b, c.a * g_alpha * extra); }

static ID2D1PathGeometry* makePoly(const Pt* p, int n, bool closed) {
    ID2D1PathGeometry* g = nullptr; if (n < 2 || FAILED(g_fac->CreatePathGeometry(&g))) return nullptr;
    ID2D1GeometrySink* sink = nullptr; if (FAILED(g->Open(&sink))) { g->Release(); return nullptr; }
    sink->BeginFigure(D2D1::Point2F(p[0].x, p[0].y), closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i < n; i++) sink->AddLine(D2D1::Point2F(p[i].x, p[i].y));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close(); sink->Release(); return g;
}

// ------------------------------------------------------------------ gx
namespace gx {
void save() { g_stack.push_back({g_cm, g_alpha, g_clips.size()}); }
void restore() {
    if (g_stack.empty()) return;
    SaveRec r = g_stack.back(); g_stack.pop_back();
    while (g_clips.size() > r.clips) {
        ClipEntry& c = g_clips.back();
        if (c.kind == 'a') g_rt->PopAxisAlignedClip(); else { g_rt->PopLayer(); g_layerDepth--; }
        SafeRelease(c.geom); g_clips.pop_back();
    }
    g_cm = r.m; g_alpha = r.alpha; applyTransform();
}
void translate(float x, float y) { g_cm = mm(mk(1, 0, 0, 1, x, y), g_cm); applyTransform(); }
void scale(float sx, float sy) { g_cm = mm(mk(sx, 0, 0, sy, 0, 0), g_cm); applyTransform(); }
void rotate(float deg) { float r = deg * 3.14159265f / 180.f, c = std::cos(r), sn = std::sin(r); g_cm = mm(mk(c, sn, -sn, c, 0, 0), g_cm); applyTransform(); }
void setAlpha(float a) { g_alpha *= a; }
static void pushGeomLayer(ID2D1Geometry* geom) {
    while ((int)g_layers.size() <= g_layerDepth) { ID2D1Layer* l = nullptr; g_rt->CreateLayer(nullptr, &l); g_layers.push_back(l); }
    D2D1_LAYER_PARAMETERS lp = {};
    lp.contentBounds = D2D1::RectF(-1e9f, -1e9f, 1e9f, 1e9f); lp.geometricMask = geom; lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    lp.maskTransform = mk(1, 0, 0, 1, 0, 0); lp.opacity = 1.f; lp.opacityBrush = nullptr; lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;
    g_rt->PushLayer(lp, g_layers[g_layerDepth]); g_layerDepth++; g_clips.push_back({'l', geom});
}
void clipRect(float x, float y, float w, float h) {
    bool axis = std::fabs(g_cm._12) < 1e-4f && std::fabs(g_cm._21) < 1e-4f;
    if (axis) { g_rt->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, y + h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE); g_clips.push_back({'a', nullptr}); return; }
    ID2D1RectangleGeometry* rg = nullptr; if (SUCCEEDED(g_fac->CreateRectangleGeometry(D2D1::RectF(x, y, x + w, y + h), &rg))) pushGeomLayer(rg);
}
void clipPoly(const Pt* p, int n) { ID2D1PathGeometry* g = makePoly(p, n, true); if (g) pushGeomLayer(g); }
void rect(float x, float y, float w, float h, Color c) { g_brush->SetColor(col(c)); g_rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), g_brush); }
void rrect(float x, float y, float w, float h, float r, Color c) {
    r = std::min(r, std::min(w, h) / 2); g_brush->SetColor(col(c));
    if (r <= .5f) g_rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), g_brush);
    else g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), g_brush);
}
void rrectStroke(float x, float y, float w, float h, float r, float lw, Color c) {
    r = std::min(r, std::min(w, h) / 2); g_brush->SetColor(col(c));
    if (r <= .5f) g_rt->DrawRectangle(D2D1::RectF(x, y, x + w, y + h), g_brush, lw);
    else g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), g_brush, lw);
}
void grad(float x, float y, float w, float h, bool vertical, const Color* cols, const float* offs, int n) {
    std::vector<D2D1_GRADIENT_STOP> st(n); for (int i = 0; i < n; i++) st[i] = {offs[i], col(cols[i])};
    ID2D1GradientStopCollection* sc = nullptr; if (FAILED(g_rt->CreateGradientStopCollection(st.data(), n, &sc))) return;
    ID2D1LinearGradientBrush* b = nullptr;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp = {vertical ? D2D1::Point2F(x, y) : D2D1::Point2F(x, y), vertical ? D2D1::Point2F(x, y + h) : D2D1::Point2F(x + w, y)};
    if (SUCCEEDED(g_rt->CreateLinearGradientBrush(lp, sc, &b))) { g_rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), b); b->Release(); }
    sc->Release();
}
void ellipse(float cx, float cy, float rx, float ry, Color c) { g_brush->SetColor(col(c)); g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), g_brush); }
void ellipseStroke(float cx, float cy, float rx, float ry, float lw, Color c) { g_brush->SetColor(col(c)); g_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), g_brush, lw); }
void line(float x1, float y1, float x2, float y2, float lw, Color c, bool round) {
    g_brush->SetColor(col(c)); g_rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), g_brush, lw, round ? g_strokeRound : g_strokeFlat);
}
void poly(const Pt* p, int n, Color c) { ID2D1PathGeometry* g = makePoly(p, n, true); if (!g) return; g_brush->SetColor(col(c)); g_rt->FillGeometry(g, g_brush); g->Release(); }
void polyStroke(const Pt* p, int n, bool closed, float lw, Color c) {
    ID2D1PathGeometry* g = makePoly(p, n, closed); if (!g) return; g_brush->SetColor(col(c)); g_rt->DrawGeometry(g, g_brush, lw, g_strokeRound); g->Release();
}
void arc(float cx, float cy, float r, float a0, float a1, float lw, Color c) {
    Pt pts[25]; for (int i = 0; i <= 24; i++) { float a = a0 + (a1 - a0) * i / 24; pts[i] = {cx + std::cos(a) * r, cy + std::sin(a) * r}; }
    polyStroke(pts, 25, false, lw, c);
}
static ID2D1Bitmap* bitmapFor(const Image* cim) {
    Image* im = const_cast<Image*>(cim);
    if (im->tex) return (ID2D1Bitmap*)im->tex;
    std::vector<uint8_t> px(im->px.size());
    for (size_t i = 0; i + 3 < px.size(); i += 4) {           // RGBA -> premultiplied BGRA
        uint32_t a = im->px[i + 3];
        px[i] = (uint8_t)(im->px[i + 2] * a / 255); px[i + 1] = (uint8_t)(im->px[i + 1] * a / 255); px[i + 2] = (uint8_t)(im->px[i] * a / 255); px[i + 3] = (uint8_t)a;
    }
    ID2D1Bitmap* bmp = nullptr;
    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(g_rt->CreateBitmap(D2D1::SizeU(im->w, im->h), px.data(), im->w * 4, bp, &bmp))) return nullptr;
    im->tex = bmp; g_uploaded.push_back(im); return bmp;
}
void image(const Image* im, float dx, float dy, float dw, float dh, float a, float sx, float sy, float sw, float sh) {
    if (!im || im->px.empty()) return;
    ID2D1Bitmap* b = bitmapFor(im); if (!b) return;
    D2D1_RECT_F src = sw < 0 ? D2D1::RectF(0, 0, (float)im->w, (float)im->h) : D2D1::RectF(sx, sy, sx + sw, sy + sh);
    D2D1_RECT_F dst = D2D1::RectF(dx, dy, dx + dw, dy + dh);
    g_rt->DrawBitmap(b, &dst, a * g_alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
}

// --- текст
static std::unordered_map<std::string, IDWriteTextFormat*> g_fmt;
static IDWriteTextFormat* fmtFor(float size, int weight, int font) {
    std::string key = std::to_string((int)(size * 10)) + ":" + std::to_string(weight) + ":" + std::to_string(font);
    auto it = g_fmt.find(key); if (it != g_fmt.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    g_dw->CreateTextFormat(font == 1 ? L"Impact" : L"Segoe UI", nullptr, (DWRITE_FONT_WEIGHT)(weight < 100 ? 400 : weight), DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ru-ru", &f);
    if (f) { f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR); f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); }
    g_fmt[key] = f; return f;
}
static IDWriteTextLayout* layoutFor(const std::string& s, float size, int weight, int font) {
    IDWriteTextFormat* f = fmtFor(size, weight, font); if (!f) return nullptr;
    std::wstring w = toW(s); IDWriteTextLayout* l = nullptr;
    if (FAILED(g_dw->CreateTextLayout(w.c_str(), (UINT32)w.size(), f, 4096.f, 1024.f, &l))) return nullptr;
    return l;
}
float textW(const std::string& s, float size, int weight, int font) {
    static std::unordered_map<std::string, float> cache;
    std::string key = s + "|" + std::to_string((int)(size * 10)) + "|" + std::to_string(weight) + "|" + std::to_string(font);
    auto it = cache.find(key); if (it != cache.end()) return it->second;
    float w = size * .5f * s.size();
    if (IDWriteTextLayout* l = layoutFor(s, size, weight, font)) { DWRITE_TEXT_METRICS m; if (SUCCEEDED(l->GetMetrics(&m))) w = m.widthIncludingTrailingWhitespace; l->Release(); }
    if (cache.size() > 4000) cache.clear();
    cache[key] = w; return w;
}
void text(const std::string& s, float x, float y, float size, Color c, int align, int valign, int weight, int font) {
    if (s.empty()) return;
    IDWriteTextLayout* l = layoutFor(s, size, weight, font); if (!l) return;
    DWRITE_TEXT_METRICS m; l->GetMetrics(&m);
    float ox = x - (align == 1 ? m.widthIncludingTrailingWhitespace / 2 : align == 2 ? m.widthIncludingTrailingWhitespace : 0), oy = y;
    if (valign == 0) { DWRITE_LINE_METRICS lm; UINT32 n = 1; l->GetLineMetrics(&lm, 1, &n); oy = y - lm.baseline; }
    else if (valign == 1) oy = y - m.height / 2;
    g_brush->SetColor(col(c));
    g_rt->DrawTextLayout(D2D1::Point2F(ox, oy), l, g_brush, (D2D1_DRAW_TEXT_OPTIONS)(1 | 4));   // NO_SNAP | ENABLE_COLOR_FONT
    l->Release();
}
}  // namespace gx

// ------------------------------------------------------------------ устройство рисования
static void releaseDeviceResources() {
    for (auto& l : g_layers) SafeRelease(l); g_layers.clear();
    for (Image* im : g_uploaded) { if (im->tex) { ((ID2D1Bitmap*)im->tex)->Release(); im->tex = nullptr; } } g_uploaded.clear();
    SafeRelease(g_brush); SafeRelease(g_rt);
}
static bool ensureTarget() {
    if (g_rt) return true;
    RECT rc; GetClientRect(g_hwnd, &rc);
    D2D1_SIZE_U sz = D2D1::SizeU(std::max<LONG>(1, rc.right - rc.left), std::max<LONG>(1, rc.bottom - rc.top));
    if (FAILED(g_fac->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(g_hwnd, sz), &g_rt))) return false;
    g_rt->SetDpi(96.f, 96.f);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &g_brush);
    return g_brush != nullptr;
}

// ------------------------------------------------------------------ звук (waveOut + собственный микшер)
static HWAVEOUT g_wo = nullptr; static WAVEHDR g_hdr[4]; static int16_t g_wbuf[4][441]; static volatile bool g_run = true;
static DWORD WINAPI audioThread(LPVOID) {
    while (g_run) {
        for (int i = 0; i < 4; i++) if (g_hdr[i].dwFlags & WHDR_DONE) {
            au::render(g_wbuf[i], 441); g_hdr[i].dwFlags &= ~WHDR_DONE; waveOutWrite(g_wo, &g_hdr[i], sizeof(WAVEHDR));
        }
        Sleep(4);
    }
    return 0;
}
static void startAudio() {
    WAVEFORMATEX f = {}; f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 1; f.nSamplesPerSec = au::RATE; f.wBitsPerSample = 16; f.nBlockAlign = 2; f.nAvgBytesPerSec = au::RATE * 2;
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { g_wo = nullptr; return; }
    for (int i = 0; i < 4; i++) {
        memset(&g_hdr[i], 0, sizeof(WAVEHDR)); g_hdr[i].lpData = (LPSTR)g_wbuf[i]; g_hdr[i].dwBufferLength = sizeof(g_wbuf[i]);
        waveOutPrepareHeader(g_wo, &g_hdr[i], sizeof(WAVEHDR)); au::render(g_wbuf[i], 441); waveOutWrite(g_wo, &g_hdr[i], sizeof(WAVEHDR));
    }
    CreateThread(nullptr, 0, audioThread, nullptr, 0, nullptr);
}

// ------------------------------------------------------------------ os
namespace os {
double nowMs() { FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime; return (double)(u.QuadPart / 10000ULL) - 11644473600000.0; }
static std::string savePath() {
    char buf[MAX_PATH]; DWORD n = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH); std::string d = n ? std::string(buf, n) : std::string(".");
    d += "\\Romchik"; CreateDirectoryA(d.c_str(), nullptr); return d + "\\save.txt";
}
std::string loadSave() { FILE* f = fopen(savePath().c_str(), "rb"); if (!f) return ""; std::string s; char b[1024]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s; }
void writeSave(const std::string& s) { FILE* f = fopen(savePath().c_str(), "wb"); if (!f) return; fwrite(s.data(), 1, s.size(), f); fclose(f); }
void setClipboard(const std::string& s) {
    std::wstring w = toW(s); if (!OpenClipboard(g_hwnd)) return; EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (h) { memcpy(GlobalLock(h), w.c_str(), (w.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
    CloseClipboard();
}
static HWAVEIN g_wi = nullptr; static WAVEHDR g_ih; static std::vector<int16_t> g_ibuf; static bool g_rec = false;
bool recording() { return g_rec; }
bool recStart() {
    if (g_rec) return true;
    WAVEFORMATEX f = {}; f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 1; f.nSamplesPerSec = au::RATE; f.wBitsPerSample = 16; f.nBlockAlign = 2; f.nAvgBytesPerSec = au::RATE * 2;
    if (waveInOpen(&g_wi, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { g_wi = nullptr; return false; }
    g_ibuf.assign(au::RATE * 7, 0); memset(&g_ih, 0, sizeof g_ih); g_ih.lpData = (LPSTR)g_ibuf.data(); g_ih.dwBufferLength = (DWORD)(g_ibuf.size() * 2);
    waveInPrepareHeader(g_wi, &g_ih, sizeof g_ih); waveInAddBuffer(g_wi, &g_ih, sizeof g_ih); waveInStart(g_wi); g_rec = true; return true;
}
std::vector<int16_t> recStop() {
    std::vector<int16_t> out; if (!g_rec) return out;
    waveInStop(g_wi); waveInReset(g_wi);
    size_t n = g_ih.dwBytesRecorded / 2; out.assign(g_ibuf.begin(), g_ibuf.begin() + std::min(n, g_ibuf.size()));
    waveInUnprepareHeader(g_wi, &g_ih, sizeof g_ih); waveInClose(g_wi); g_wi = nullptr; g_rec = false; return out;
}
}

// ------------------------------------------------------------------ окно
static void updateLetterbox() {
    RECT rc; GetClientRect(g_hwnd, &rc); float cw = (float)(rc.right - rc.left), ch = (float)(rc.bottom - rc.top);
    g_scale = std::min(cw / gx::W, ch / gx::H); if (g_scale <= 0) g_scale = 1;
    g_offX = (cw - gx::W * g_scale) / 2; g_offY = (ch - gx::H * g_scale) / 2;
    g_base = mk(g_scale, 0, 0, g_scale, g_offX, g_offY);
}
static void mapPt(LPARAM lp, float& x, float& y) { x = ((float)(short)LOWORD(lp) - g_offX) / g_scale; y = ((float)(short)HIWORD(lp) - g_offY) / g_scale; }
static bool g_down = false;
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    float x, y;
    switch (m) {
    case WM_SIZE: if (g_rt) g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp))); updateLetterbox(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: ValidateRect(h, nullptr); return 0;
    case WM_LBUTTONDOWN: SetCapture(h); g_down = true; mapPt(lp, x, y); game_down(x, y); return 0;
    case WM_MOUSEMOVE: mapPt(lp, x, y); game_move(x, y); return 0;
    case WM_LBUTTONUP: if (g_down) { g_down = false; ReleaseCapture(); mapPt(lp, x, y); game_up(x, y); } return 0;
    case WM_CAPTURECHANGED: if (g_down) { g_down = false; game_up(-1000, -1000); } return 0;
    case WM_MOUSEWHEEL: { POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)}; ScreenToClient(h, &p); game_wheel((p.x - g_offX) / g_scale, (p.y - g_offY) / g_scale, -(float)GET_WHEEL_DELTA_WPARAM(wp) / 120.f * 60.f); return 0; }
    case WM_KEYDOWN: case WM_KEYUP: {
        bool dn = m == WM_KEYDOWN; if (dn && (lp & (1 << 30))) return 0;
        if (wp == VK_SPACE) game_key(0, dn); else if (wp == VK_LEFT) game_key(1, dn); else if (wp == VK_RIGHT) game_key(2, dn);
        return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool loadAssets() {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(1), RT_RCDATA);
    if (r) { HGLOBAL g = LoadResource(nullptr, r); if (g) return assets_load((const uint8_t*)LockResource(g), SizeofResource(nullptr, r)); }
    FILE* f = fopen("assets.bin", "rb"); if (!f) f = fopen("assets\\assets.bin", "rb"); if (!f) return false;
    std::vector<uint8_t> d; uint8_t b[65536]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n); fclose(f);
    return assets_load(d.data(), d.size());
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
    SetProcessDPIAware();
    if (!loadAssets()) { MessageBoxW(nullptr, L"Не удалось загрузить ресурсы игры (assets.bin).", L"Ромчик красавчик", MB_ICONERROR); return 1; }
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_fac))) { MessageBoxW(nullptr, L"Direct2D недоступен.", L"Ромчик красавчик", MB_ICONERROR); return 1; }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw))) return 1;
    { D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.f, D2D1_DASH_STYLE_SOLID, 0.f);
      g_fac->CreateStrokeStyle(sp, nullptr, 0, &g_strokeRound);
      sp.startCap = sp.endCap = sp.dashCap = D2D1_CAP_STYLE_FLAT; g_fac->CreateStrokeStyle(sp, nullptr, 0, &g_strokeFlat); }

    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"RomchikWnd"; wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);
    int sh = GetSystemMetrics(SM_CYSCREEN); int ch = std::min(800, (int)(sh * .82f)), cw = (int)(ch * gx::W / gx::H);
    RECT r = {0, 0, cw, ch}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowW(L"RomchikWnd", L"Ромчик красавчик", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hi, nullptr);
    ShowWindow(g_hwnd, show); UpdateWindow(g_hwnd); updateLetterbox();
    startAudio(); game_init();

    LARGE_INTEGER fq, last; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&last);
    MSG msg; bool quit = false;
    while (!quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) quit = true; TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (quit) break;
        if (IsIconic(g_hwnd)) { Sleep(50); QueryPerformanceCounter(&last); continue; }
        if (!ensureTarget()) { Sleep(100); continue; }
        LARGE_INTEGER now; QueryPerformanceCounter(&now); float dt = (float)((double)(now.QuadPart - last.QuadPart) / fq.QuadPart); last = now;
        g_stack.clear(); g_clips.clear(); g_layerDepth = 0; g_cm = mk(1, 0, 0, 1, 0, 0); g_alpha = 1.f; updateLetterbox();
        g_rt->BeginDraw(); { D2D1_MATRIX_3X2_F id = mk(1, 0, 0, 1, 0, 0); g_rt->SetTransform(id); }
        g_rt->Clear(D2D1::ColorF(0.06f, 0.06f, 0.08f));
        g_rt->SetTransform(g_base);
        g_rt->PushAxisAlignedClip(D2D1::RectF(0, 0, gx::W, gx::H), D2D1_ANTIALIAS_MODE_ALIASED);
        game_frame(dt);
        while (!g_stack.empty()) gx::restore();
        g_rt->PopAxisAlignedClip();
        HRESULT hr = g_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) releaseDeviceResources();
    }
    g_run = false; game_shutdown(); if (g_wo) { waveOutReset(g_wo); }
    return 0;
}
