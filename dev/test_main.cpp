// Тестовый бэкенд для Linux: без окна. Пишет команды рисования в JSON-лог (рендерит tools/render_log.py)
// и прогоняет игру по сценарию (дымовой тест).
#include "internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

struct M { float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; };
static M cm; static std::vector<M> stk; static float galpha = 1; static std::vector<float> astk;
static std::vector<std::vector<Pt>> clips; static std::vector<size_t> cstk;
static FILE* lg = nullptr; static bool logging = false; static std::map<const Image*, int> imgId; static std::vector<const Image*> imgList;
static Pt tp(float x, float y) { return {cm.a * x + cm.c * y + cm.e, cm.b * x + cm.d * y + cm.f}; }
static M mul(const M& m, const M& n) { return {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d, m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f}; }
static float msc() { return std::sqrt(std::fabs(cm.a * cm.d - cm.b * cm.c)); }
static void ptsJ(const std::vector<Pt>& v) { fprintf(lg, "["); for (size_t i = 0; i < v.size(); i++) fprintf(lg, "%s[%.1f,%.1f]", i ? "," : "", v[i].x, v[i].y); fprintf(lg, "]"); }
static void tail(Color c) {
    fprintf(lg, ",\"c\":[%d,%d,%d,%.3f],\"clip\":[", (int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), c.a * galpha);
    for (size_t i = 0; i < clips.size(); i++) { if (i) fprintf(lg, ","); ptsJ(clips[i]); } fprintf(lg, "]}\n");
}
static void shape(const char* k, const std::vector<Pt>& v, Color c, float lw = 0, bool closed = true) {
    if (!logging) return; std::vector<Pt> t; for (auto& p : v) t.push_back(tp(p.x, p.y));
    fprintf(lg, "{\"k\":\"%s\",\"lw\":%.2f,\"cl\":%d,\"p\":", k, lw * msc(), closed); ptsJ(t); tail(c);
}
static std::vector<Pt> rrPts(float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2); std::vector<Pt> v; const float cx[4] = {x + w - r, x + w - r, x + r, x + r}, cy[4] = {y + r, y + h - r, y + h - r, y + r};
    for (int k = 0; k < 4; k++) for (int i = 0; i <= 4; i++) { float a = (k * 90 + i * 22.5f) * 3.14159265f / 180 - 1.5707963f; v.push_back({cx[k] + std::cos(a) * r, cy[k] + std::sin(a) * r}); }
    return v;
}
static std::vector<Pt> ellPts(float cx, float cy, float rx, float ry, int n = 28) { std::vector<Pt> v; for (int i = 0; i < n; i++) { float a = i * 6.2831853f / n; v.push_back({cx + std::cos(a) * rx, cy + std::sin(a) * ry}); } return v; }
namespace gx {
void save() { stk.push_back(cm); astk.push_back(galpha); cstk.push_back(clips.size()); }
void restore() { if (stk.empty()) { fprintf(stderr, "restore underflow\n"); abort(); } cm = stk.back(); stk.pop_back(); galpha = astk.back(); astk.pop_back(); clips.resize(cstk.back()); cstk.pop_back(); }
void translate(float x, float y) { cm = mul(cm, {1, 0, 0, 1, x, y}); }
void scale(float sx, float sy) { cm = mul(cm, {sx, 0, 0, sy, 0, 0}); }
void rotate(float deg) { float r = deg * 3.14159265f / 180, c = std::cos(r), s = std::sin(r); cm = mul(cm, {c, s, -s, c, 0, 0}); }
void clipPoly(const Pt* p, int n) { std::vector<Pt> c; for (int i = 0; i < n; i++) c.push_back(tp(p[i].x, p[i].y)); clips.push_back(c); }
void clipRect(float x, float y, float w, float h) { Pt p[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}; clipPoly(p, 4); }
void setAlpha(float a) { galpha *= a; }
void rect(float x, float y, float w, float h, Color c) { shape("poly", {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, c); }
void rrect(float x, float y, float w, float h, float r, Color c) { shape("poly", rrPts(x, y, w, h, r), c); }
void rrectStroke(float x, float y, float w, float h, float r, float lw, Color c) { shape("line", rrPts(x, y, w, h, r), c, lw); }
void grad(float x, float y, float w, float h, bool vert, const Color* cols, const float* offs, int n) {
    const int N = 20;
    for (int i = 0; i < N; i++) {
        float t = (i + .5f) / N; int k = 0; while (k < n - 2 && t > offs[k + 1]) k++;
        float u = offs[k + 1] > offs[k] ? std::max(0.f, std::min(1.f, (t - offs[k]) / (offs[k + 1] - offs[k]))) : 0.f;
        Color c{cols[k].r + (cols[k + 1].r - cols[k].r) * u, cols[k].g + (cols[k + 1].g - cols[k].g) * u, cols[k].b + (cols[k + 1].b - cols[k].b) * u, cols[k].a};
        if (vert) rect(x, y + h * i / N, w, h / N + 1, c); else rect(x + w * i / N, y, w / N + 1, h, c);
    }
}
void ellipse(float cx, float cy, float rx, float ry, Color c) { shape("poly", ellPts(cx, cy, rx, ry), c); }
void ellipseStroke(float cx, float cy, float rx, float ry, float lw, Color c) { shape("line", ellPts(cx, cy, rx, ry), c, lw); }
void arc(float cx, float cy, float r, float a0, float a1, float lw, Color c) { std::vector<Pt> v; for (int i = 0; i <= 16; i++) { float a = a0 + (a1 - a0) * i / 16; v.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r}); } shape("line", v, c, lw, false); }
void line(float x1, float y1, float x2, float y2, float lw, Color c, bool) { shape("line", {{x1, y1}, {x2, y2}}, c, lw, false); }
void poly(const Pt* p, int n, Color c) { shape("poly", std::vector<Pt>(p, p + n), c); }
void polyStroke(const Pt* p, int n, bool closed, float lw, Color c) { shape("line", std::vector<Pt>(p, p + n), c, lw, closed); }
void image(const Image* im, float dx, float dy, float dw, float dh, float a, float sx, float sy, float sw, float sh) {
    if (!logging || !im) return; if (sw < 0) { sw = (float)im->w; sh = (float)im->h; }
    int id; auto it = imgId.find(im); if (it == imgId.end()) { id = (int)imgList.size(); imgId[im] = id; imgList.push_back(im); } else id = it->second;
    std::vector<Pt> t{tp(dx, dy), tp(dx + dw, dy), tp(dx + dw, dy + dh), tp(dx, dy + dh)};
    fprintf(lg, "{\"k\":\"img\",\"id\":%d,\"src\":[%.1f,%.1f,%.1f,%.1f],\"p\":", id, sx, sy, sw, sh); ptsJ(t); tail({1, 1, 1, a});
}
void text(const std::string& s, float x, float y, float size, Color c, int al, int va, int wt, int font) {
    if (!logging) return; Pt p = tp(x, y); std::string e;
    for (char ch : s) { if (ch == '"' || ch == '\\') e += '\\'; if ((unsigned char)ch >= 32) e += ch; }
    fprintf(lg, "{\"k\":\"txt\",\"s\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"sz\":%.1f,\"al\":%d,\"va\":%d,\"wt\":%d,\"f\":%d,\"rot\":%.1f", e.c_str(), p.x, p.y, size * msc(), al, va, wt, font, std::atan2(cm.b, cm.a) * 57.2958f);
    tail(c);
}
float textW(const std::string& s, float size, int, int) { float w = 0; for (size_t i = 0; i < s.size();) { unsigned char c = s[i]; int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; w += (n == 4 ? 1.2f : n == 3 ? .5f : n == 2 ? .58f : .52f) * size; i += n; } return w; }
}
namespace os {
static double fakeNow = 1.7e12;
double nowMs() { return fakeNow; }
std::string loadSave() { std::ifstream f("/tmp/romchik_save.txt"); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
void writeSave(const std::string& s) { std::ofstream("/tmp/romchik_save.txt") << s; }
static bool rec = false;
bool recStart() { rec = true; return true; } bool recording() { return rec; }
std::vector<int16_t> recStop() { rec = false; std::vector<int16_t> v(22050 * 2); for (size_t i = 0; i < v.size(); i++) v[i] = (int16_t)(8000 * std::sin(i * .05)); return v; }
void setClipboard(const std::string&) {}
}

// ---------------------------------------------------------------- сценарий
static int frameNo = 0;
static void frame(const char* shot = nullptr, float dt = 1 / 60.f) {
    cm = M(); stk.clear(); astk.clear(); cstk.clear(); clips.clear(); galpha = 1;
    logging = shot != nullptr;
    if (logging) { std::string fn = std::string("/tmp/shots/") + shot + ".jsonl"; lg = fopen(fn.c_str(), "w"); }
    game_frame(dt);
    if (stk.size()) { fprintf(stderr, "save/restore imbalance: %zu\n", stk.size()); abort(); }
    if (logging) { fclose(lg); logging = false; }
    os::fakeNow += dt * 1000; frameNo++;
    int16_t buf[512]; au::render(buf, 512);
}
static void run(int n) { for (int i = 0; i < n; i++) frame(); }
static void click(float x, float y, int hold = 3) { game_down(x, y); run(hold); game_up(x, y); run(2); }
static void drag(float x0, float y0, float x1, float y1, int steps = 20) { game_down(x0, y0); for (int i = 1; i <= steps; i++) { game_move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps); frame(); } game_up(x1, y1); run(2); }

int main(int argc, char** argv) {
    const char* assets = argc > 1 ? argv[1] : "assets/assets.bin";
    std::ifstream f(assets, std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    if (!assets_load(d.data(), d.size())) { fprintf(stderr, "assets load failed\n"); return 1; }
    system("mkdir -p /tmp/shots; rm -f /tmp/romchik_save.txt");
    game_init(); run(30);
    frame("00_living");
    const char* names[8] = {"living", "games", "kitchen", "bedroom", "toilet", "pond", "bbq", "bar"};
    for (int r = 0; r < 8; r++) { click(r * 56.25f + 28, 770); run(60); std::string n = std::string("10_room_") + names[r]; frame(n.c_str()); }
    // персонаж: клики по частям тела
    click(225, 770 - 62 * 0 + 0); click(28, 770); run(30);
    click(225, 300); run(10); frame("11_poke_head"); click(225, 420); run(20); click(100, 420); click(225, 700); run(20);
    // кухня: покупка, прокрутка
    click(3 * 56.25f + 28 - 112, 770); run(30); // kitchen
    click(3 * 56.25f - 56.25f + 28 - 56.25f, 770); run(5);
    click(2 * 56.25f + 28, 770); run(60);
    drag(300, 650, 100, 650); frame("12_kitchen_scroll");
    click(60, 650); run(60); frame("13_fed");
    // спальня
    click(3 * 56.25f + 28, 770); run(30); S.energy = 40; frame("14_bed_ui"); click(120, 700); run(200); frame("15_sleeping"); click(225, 520); run(30);
    
    // бар и компания
    click(7 * 56.25f + 28, 770); run(60); frame("20_bar"); click(300, 700); run(20); click(70, 120); click(150, 120); run(10); click(70, 120); click(70, 120); click(70, 120); run(60); frame("21_bar_drunk");
    // шашлык
    click(6 * 56.25f + 28, 770); run(30); frame("22_bbq");
    // рыбалка
    click(5 * 56.25f + 28, 770); run(30);
    for (int attempt = 0; attempt < 8; attempt++) {
        click(225, 700); run(10);
        for (int i = 0; i < 700; i++) { game_key(0, (i / 6) % 2 == 0); frame(i == 40 ? "30_fish_wait" : (attempt == 0 && (i == 300 || i == 360)) ? (i == 300 ? "31_fish_a" : "31_fish_b") : nullptr); }
        game_key(0, false);
    }
    frame("32_fish_after");
    click(225, 640); run(20);
    // игры: по очереди открываем все, тапаем случайно, закрываем
    click(1 * 56.25f + 28, 770); run(30); frame("40_games_panel");
    for (int g = 0; g < 12; g++) {
        game_frame(0); overlayOpen((GameId)g); run(10);
        char nm[32]; snprintf(nm, sizeof nm, "5%d_game_%d", g / 10, g % 10 + (g / 10) * 0); std::string tag = "50_game_" + std::to_string(g);
        for (int i = 0; i < 900; i++) {
            if (i % 7 == 0) { float x = 20 + (i * 37) % 400, y = 100 + (i * 91) % 600; game_down(x, y); game_move(x + 10, y + 10); game_up(x, y); }
            if (i % 50 == 0) { game_key(1, true); game_key(2, true); }
            frame(i == 300 ? tag.c_str() : nullptr);
        }
        game_down(430, 70); game_up(430, 70); run(5);       // ✖
    }
    // результат игры: открываем «Тир» и проигрываем до конца
    overlayOpen(G_SHOOT); for (int i = 0; i < 60 * 30; i++) frame(i == 60 * 28 ? "60_result" : nullptr); click(225, 500); run(5);
    // голос
    click(0 * 56.25f + 28, 770); run(10); game_down(70, 700); run(30); game_up(70, 700); run(120); frame("70_after_mic");
    game_shutdown();
    printf("OK, frames=%d\n", frameNo);
}
