// Интерфейс между игрой (портативный C++17) и платформой (Win32/Direct2D или тестовый бэкенд).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Color { float r = 0, g = 0, b = 0, a = 1; };
inline Color rgb(uint32_t hex, float a = 1.f) { return { ((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a }; }
inline Color rgba8(uint32_t hex, int a8) { return rgb(hex, a8 / 255.f); }
inline Color alpha(Color c, float a) { c.a = a; return c; }
struct Pt { float x, y; };

struct Image {                       // пиксели RGBA (не premultiplied)
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    void* tex = nullptr;             // дескриптор бэкенда (создаётся лениво)
};

namespace gx {
constexpr float W = 450.f, H = 800.f;   // виртуальный экран (портрет), 1 vmin = 4.5
void save(); void restore();
void translate(float x, float y); void scale(float sx, float sy); void rotate(float deg);
void clipRect(float x, float y, float w, float h);       // действует до restore()
void clipPoly(const Pt* p, int n);
void setAlpha(float a);                                   // глобальная прозрачность (как globalAlpha)
void rect(float x, float y, float w, float h, Color c);
void rrect(float x, float y, float w, float h, float r, Color c);
void rrectStroke(float x, float y, float w, float h, float r, float lw, Color c);
void grad(float x, float y, float w, float h, bool vertical, const Color* cols, const float* offs, int n);
void ellipse(float cx, float cy, float rx, float ry, Color c);
void ellipseStroke(float cx, float cy, float rx, float ry, float lw, Color c);
void arc(float cx, float cy, float r, float a0, float a1, float lw, Color c);
void line(float x1, float y1, float x2, float y2, float lw, Color c, bool round = true);
void poly(const Pt* p, int n, Color c);
void polyStroke(const Pt* p, int n, bool closed, float lw, Color c);
void image(const Image* im, float dx, float dy, float dw, float dh, float a = 1, float sx = 0, float sy = 0, float sw = -1, float sh = -1);
// align: 0 лево, 1 центр, 2 право; valign: 0 baseline, 1 middle, 2 top; font: 0 UI, 1 Impact
void text(const std::string& s, float x, float y, float size, Color c, int align = 1, int valign = 0, int weight = 400, int font = 0);
float textW(const std::string& s, float size, int weight = 400, int font = 0);
}

namespace au {
struct Clip { const int16_t* pcm = nullptr; int n = 0; int rate = 22050; float dur() const { return rate ? (float)n / rate : 0.f; } };
const Clip* clip(const std::string& name);                 // из assets.bin
int  play(const Clip* c, int channel, float speed = 1.f, float gain = 1.f);   // channel>=0 заменяет звук на этом канале
int  playPCM(std::vector<int16_t> pcm, int channel, float speed, float gain);
void stopChannel(int channel);
bool playing(int channel);
float level(int channel);                                  // 0..1, громкость «сейчас»
void tone(float freq, float dur, int type /*0 sine,1 square,2 saw,3 tri*/, float vol, float slideTo = 0, float delay = 0);
void render(int16_t* out, int n);                          // вызывается аудио-потоком бэкенда
constexpr int RATE = 22050;
}

namespace os {
double nowMs();                                            // unix-время, мс
std::string loadSave(); void writeSave(const std::string& s);
bool recStart(); std::vector<int16_t> recStop(); bool recording();   // микрофон, 22050 Гц mono
void setClipboard(const std::string& s);
}

// ресурсы
const Image* asset_image(const std::string& name);
bool assets_load(const uint8_t* data, size_t size);

// вход в игру
void game_init();
void game_frame(float dt);
void game_down(float x, float y);
void game_move(float x, float y);
void game_up(float x, float y);
void game_wheel(float x, float y, float delta);
void game_key(int code, bool down);        // 0 Space, 1 Left, 2 Right
void game_shutdown();
