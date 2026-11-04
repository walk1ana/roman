// Мини-игры: холст 360×520 внутри затемнённого оверлея.
#include "internal.h"
#include <map>
#include <memory>

namespace {
const float CW = 360, CH = 520, SC = 1.2f, OX = 9, OY = 90;   // холст на экране: 432×624
float gDt = 0;
bool gDown = false; Pt gPtr{0, 0};
void E(const std::string& s, float x, float y, float size) { emo(s, x, y, size); }
void T(const std::string& s, float x, float y, float size = 18, Color c = rgb(0xffffff)) { txt(s, x, y, size, c, 800); }
void Tl(const std::string& s, float x, float y, float size, Color c) { gx::text(s, x, y, size, c, 0, 0, 800); }
void FX(const std::string& s, float x, float y, float size, Color c, float a) { gx::save(); gx::setAlpha(std::max(0.f, std::min(1.f, a))); T(s, x, y, size, c); gx::restore(); }
void vgr(float x, float y, float w, float h, uint32_t a, uint32_t b) { Color c[2] = {rgb(a), rgb(b)}; float o[2] = {0, 1}; gx::grad(x, y, w, h, true, c, o, 2); }
float rnd(float a, float b) { return frand(a, b); }
void addReward(int n) { S.coins = std::max(0, S.coins + n); saveGame(); }
bool inR(Pt p, float x, float y, float w, float h) { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
void quad(std::vector<Pt>& v, Pt a, Pt c, Pt b) { for (int i = 1; i <= 8; i++) { float u = i / 8.f, m = 1 - u; v.push_back({m * m * a.x + 2 * m * u * c.x + u * u * b.x, m * m * a.y + 2 * m * u * c.y + u * u * b.y}); } }
void rcPlay(int n) { au::play(au::clip(std::to_string(n)), 200 + n); }
void rcStop() { for (int n = 31; n <= 37; n++) au::stopChannel(200 + n); }
float rcDur(int n) { auto c = au::clip(std::to_string(n)); return c ? c->dur() : 3.6f; }
struct Btn { float x, y, w, h; std::string label; bool primary = true; };
void drawBtn(const Btn& b, bool dis = false) {
    float a = dis ? .5f : 1.f;
    gx::rrect(b.x, b.y + 2, b.w, b.h, 8, rgba8(0x000000, 0x40 * a)); gx::rrect(b.x, b.y, b.w, b.h, 8, b.primary ? rgb(0xffd23a, a) : rgb(0xe8edf5, a));
    gx::text(b.label, b.x + b.w / 2, b.y + b.h / 2, 13, rgb(0x222222, a), 1, 1, 800);
}
}

struct Game {
    virtual ~Game() {}
    virtual const char* title() = 0;
    virtual std::string hint() = 0;
    virtual void init() = 0;
    virtual bool tick(float dt) = 0;                    // рисует кадр; true — игра закончена
    virtual void down(Pt) {} virtual void move(Pt) {} virtual void up(Pt) {}
    virtual void key(int, bool) {}
    virtual std::string info() = 0;
    virtual void result(std::string&, std::string&, int&) {}
    virtual bool modal() { return false; }              // без экрана результата
    virtual std::vector<std::string> acts() { return {}; }
    virtual void act(int) {}
    virtual void onClose() {}
};

// ============================================================ ТИР
struct Shoot : Game {
    struct It { float x, y, life; bool bad; }; struct Fx { std::string s; float x, y, t; Color c; };
    float t, sp; int score; std::vector<It> items; std::vector<Fx> fx;
    const char* title() override { return "🎯 Тир"; }
    std::string hint() override { return "Сбивай бутылки! Бананы не трогай 🍌"; }
    void init() override { t = 25; score = 0; sp = 0; items.clear(); fx.clear(); }
    bool tick(float dt) override {
        t -= dt; sp -= dt;
        if (sp <= 0 && t > 0) { sp = .5f + frand() * .3f; items.push_back({40 + frand() * 280, 90 + frand() * 340, 1.3f, frand() < .2f}); }
        vgr(0, 0, CW, CH, 0x1c3a24, 0x3b2a1a);
        for (auto& i : items) { i.life -= dt; float sc = std::min(1.f, i.life * 4); if (sc <= 0) continue; gx::save(); gx::translate(i.x, i.y); gx::scale(sc, sc);
            if (i.bad) E("🍌", 0, 0, 44); else if (bottleImage()) gx::image(bottleImage(), -19, -42, 38, 85); gx::restore(); }
        items.erase(std::remove_if(items.begin(), items.end(), [](const It& i) { return i.life <= 0; }), items.end());
        for (auto& f : fx) { f.t -= dt; FX(f.s, f.x, f.y - (1 - f.t) * 40, 22, f.c, f.t * 2); }
        fx.erase(std::remove_if(fx.begin(), fx.end(), [](const Fx& f) { return f.t <= 0; }), fx.end());
        return t <= 0;
    }
    void down(Pt p) override {
        for (auto& i : items) if (std::fabs(p.x - i.x) < 30 && std::fabs(p.y - i.y) < 50) {
            i.life = 0;
            if (i.bad) { score -= 2; fx.push_back({"-2", i.x, i.y, .5f, rgb(0xff6b6b)}); sfx::nope(); } else { score++; fx.push_back({"+1", i.x, i.y, .5f, rgb(0x8dff6a)}); sfx::pop(); }
            break; }
    }
    std::string info() override { return "⏱ " + std::to_string(std::max(0, (int)t)) + "  ·  " + std::to_string(score); }
    void result(std::string& a, std::string& b, int& c) override { int s = std::max(0, score); a = "Сбито: " + std::to_string(s); b = ""; c = s; S.energy = clampf(S.energy - 3, 0, 100); }
};

// ============================================================ ЛОВИ ЕДУ
struct Catch : Game {
    struct It { float x, y, v; std::string e; bool bad; };
    float t, sp, x, tx; int score; std::vector<It> items;
    const char* title() override { return "🎮 Лови еду"; }
    std::string hint() override { return "Води пальцем — лови еду, ботинки и бомбы обходи"; }
    void init() override { t = 25; score = 0; sp = 0; x = tx = CW / 2; items.clear(); }
    bool tick(float dt) override {
        t -= dt; sp -= dt; x += (tx - x) * std::min(1.f, dt * 12);
        if (sp <= 0 && t > 0) { sp = .35f + frand() * .3f; bool bad = frand() < .22f;
            items.push_back({30 + frand() * 300, -20, 150 + frand() * 100 + (25 - t) * 5, bad ? pickS({"🥾", "💣"}) : pickS({"🍔", "🍕", "🍎", "🍌", "🥟", "🍗", "🍩", "🍣"}), bad}); }
        gx::rect(0, 0, CW, CH, rgb(0xf0dcb4)); gx::rect(0, CH - 50, CW, 50, rgb(0x9a6b3c));
        for (auto& i : items) { i.y += i.v * dt; E(i.e, i.x, i.y, 36);
            if (i.y > CH - 110 && i.y < CH - 40 && std::fabs(i.x - x) < 38) { i.y = 999; if (i.bad) { score = std::max(0, score - 2); sfx::nope(); } else { score++; sfx::pop(); } } }
        items.erase(std::remove_if(items.begin(), items.end(), [](const It& i) { return i.y >= CH + 20; }), items.end());
        if (headImage()) gx::image(headImage(), x - 34, CH - 120, 68, 74);
        return t <= 0;
    }
    void move(Pt p) override { tx = clampf(p.x, 30, CW - 30); } void down(Pt p) override { tx = clampf(p.x, 30, CW - 30); }
    std::string info() override { return "⏱ " + std::to_string(std::max(0, (int)t)) + "  ·  " + std::to_string(score); }
    void result(std::string& a, std::string& b, int& c) override { a = "Поймано: " + std::to_string(score); b = ""; c = score; S.hunger = clampf(S.hunger + score * 1.5f, 0, 100); }
};

// ============================================================ ОВЦЫ
struct Sheep : Game {
    int n, clean; float x, v, jt; bool crashed; std::string msg; float mt;
    const char* title() override { return "🐑 Считай овец"; }
    std::string hint() override { return "Тапай — овца прыгает через забор. Не врежься!"; }
    void init() override { n = 0; clean = 0; x = -30; v = 150; jt = -1; crashed = false; msg = ""; mt = 0; }
    bool tick(float dt) override {
        float gy = CH - 150, fx = CW / 2;
        gx::rect(0, 0, CW, CH, rgb(0x2a2f66)); for (int i = 0; i < 20; i++) gx::rect((float)((i * 97) % (int)CW), (float)((i * 53) % 200), 2, 2, rgb(0xffffff));
        E("🌙", 300, 60, 48); gx::rect(0, gy + 20, CW, CH - gy, rgb(0x2e6b3a));
        gx::rect(fx - 8, gy - 46, 16, 66, rgb(0xd8b27a)); gx::rect(fx - 30, gy - 30, 60, 8, rgb(0xd8b27a));
        x += v * dt; float oy = 0;
        if (jt >= 0) { jt += dt; float k = jt / .75f; if (k >= 1) jt = -1; else oy = std::sin(k * PI_) * 90; }
        if (!crashed && std::fabs(x - fx) < 24 && oy < 44) { crashed = true; v = 0; mt = 1; msg = "Бац! 💥"; sfx::nope(); }
        E("🐑", x, gy - 10 - oy, 46);
        if (mt > 0) { mt -= dt; if (mt <= 0) { n++; x = -30; v = 140 + frand() * 90 + n * 8; crashed = false; msg = ""; jt = -1; } }
        else if (x > CW + 40) { n++; if (!crashed) { clean++; sfx::pop(); } x = -30; v = 140 + frand() * 90 + n * 8; jt = -1; }
        if (!msg.empty()) T(msg, CW / 2, gy - 100, 30, rgb(0xffd23a));
        T("💤 " + std::to_string(clean), CW / 2, 40, 26);
        return n >= 10;
    }
    void down(Pt) override { if (jt < 0 && !crashed && x < CW / 2) jt = 0; }
    std::string info() override { return "🐑 " + std::to_string(std::min(10, n)) + "/10"; }
    void result(std::string& a, std::string& b, int& c) override { a = "Перепрыгнули: " + std::to_string(clean) + " из 10"; b = ""; c = clean; S.energy = clampf(S.energy + clean * 2.5f, 0, 100); }
};

// ============================================================ ЛОВИ КРАСОТОК
struct Girls : Game {
    struct G { int g; float x, y, v, ph; }; struct Fx { float x, y, t; };
    float t, sp; std::vector<G> list; std::vector<int> caught; std::vector<Fx> fx; std::vector<std::string> nw;
    const char* title() override { return "💘 Лови красоток"; }
    std::string hint() override { return "Тапай по девчонкам — собери компанию!"; }
    void init() override { t = 25; sp = 0; list.clear(); caught.clear(); fx.clear(); }
    bool tick(float dt) override {
        t -= dt; sp -= dt;
        if (sp <= 0 && t > 2 && list.size() < 4) { sp = .55f + frand() * .5f; int dir = frand() < .5f ? 1 : -1; list.push_back({irand(6), dir > 0 ? -30.f : CW + 30, 110 + frand() * 320, dir * (70 + frand() * 90), frand() * 6}); }
        vgr(0, 0, CW, CH, 0x3a1d5c, 0xa8457f);
        for (auto& g : list) { g.x += g.v * dt; g.ph += dt * 6; float by = std::sin(g.ph) * 4; if (girlPhoto(g.g)) gx::image(girlPhoto(g.g), g.x - 30, g.y - 33 + by, 60, 66); T(girlName(g.g), g.x, g.y + 46, 13); }
        list.erase(std::remove_if(list.begin(), list.end(), [](const G& g) { return g.x <= -60 || g.x >= CW + 60; }), list.end());
        for (auto& f : fx) { f.t -= dt; gx::save(); gx::setAlpha(std::max(0.f, f.t * 2)); E("💘", f.x, f.y - (.7f - f.t) * 50, 34); gx::restore(); }
        fx.erase(std::remove_if(fx.begin(), fx.end(), [](const Fx& f) { return f.t <= 0; }), fx.end());
        return t <= 0;
    }
    void down(Pt p) override {
        for (int i = (int)list.size() - 1; i >= 0; i--) if (std::fabs(p.x - list[i].x) < 36 && std::fabs(p.y - list[i].y) < 40) { caught.push_back(list[i].g); fx.push_back({list[i].x, list[i].y, .7f}); list.erase(list.begin() + i); sfx::coin(); break; }
    }
    std::string info() override { return "⏱ " + std::to_string(std::max(0, (int)t)) + " · 💘 " + std::to_string(caught.size()); }
    void result(std::string& a, std::string& b, int& c) override {
        std::vector<int> ids; for (int g : caught) if (std::find(ids.begin(), ids.end(), g) == ids.end()) ids.push_back(g);
        std::vector<int> fresh; for (int g : ids) if (std::find(S.girls.begin(), S.girls.end(), girlId(g)) == S.girls.end()) fresh.push_back(g);
        a = "Поймано: " + std::to_string(caught.size());
        if (!fresh.empty()) { a += " · новые подруги: "; for (size_t i = 0; i < fresh.size(); i++) a += (i ? ", " : "") + std::string(girlName(fresh[i])); }
        b = ""; c = (int)caught.size() * 2 + (int)fresh.size() * 5;
        for (int g : fresh) S.girls.push_back(girlId(g));
    }
};

// ============================================================ ШАШЛЫК
struct Grill : Game {
    struct Sk { float a, b; int side; float rate; int st; float wt; }; struct Fx { std::string s; float x, y, t; Color c; };
    float t; int perf, burnt; Sk sk[4]; std::vector<Fx> fx;
    const char* title() override { return "🔥 Жарим шашлык"; }
    std::string hint() override { return "Слева — перевернуть, справа — снять. Снимай, когда обе стороны в зелёной зоне. Не сожги!"; }
    void init() override { t = 40; perf = burnt = 0; fx.clear(); for (auto& k : sk) k = {0, 0, 0, 11 + frand() * 7, 0, 0}; }
    static Color col(float v) { float k = std::min(1.f, v / 100); return rgb(((int)(229 - (229 - 58) * k) << 16) | ((int)(107 - (107 - 42) * k) << 8) | (int)(111 - (111 - 32) * k)); }
    bool tick(float dt) override {
        t -= dt; gx::rect(0, 0, CW, CH, rgb(0x2a1c14));
        for (int i = 0; i < 4; i++) {
            Sk& k = sk[i]; float y0 = 22.f + i * 122;
            vgr(8, y0, CW - 16, 110, 0x5a1a0a, 0xff8a1f); for (float x = 14; x < CW - 10; x += 22) gx::rect(x, y0, 5, 110, rgb(0x222222));
            if (k.st == 0) { (k.side == 0 ? k.a : k.b) += k.rate * dt;
                if (std::max(k.a, k.b) >= 105) { k.st = 1; k.wt = 1.6f; burnt++; playClip("15"); fx.push_back({"СГОРЕЛ! 💥", CW / 2, y0 + 55, 1.2f, rgb(0xff6b6b)}); } }
            else { k.wt -= dt; if (k.wt <= 0) k = {0, 0, 0, 11 + frand() * 7 + perf * .6f, 0, 0}; }
            float up = k.side == 0 ? k.b : k.a, vis = k.st == 1 ? 120 : up;
            gx::rect(24, y0 + 50, CW - 130, 6, rgb(0xd8b27a));
            for (int m = 0; m < 4; m++) gx::rrect(36.f + m * 50, y0 + 30, 44, 46, 12, k.st == 1 ? rgb(0x111111) : col(vis));
            const char* lb[2] = {"A", "B"}; float vv[2] = {k.a, k.b};
            for (int j = 0; j < 2; j++) { float bx = CW - 86, by = y0 + 26 + j * 30; gx::rect(bx, by, 70, 14, rgba8(0x000000, 0x88)); gx::rect(bx + 49, by, 21, 14, rgba8(0x3ddc5a, 0x55)); gx::rect(bx, by, std::min(70.f, 70 * vv[j] / 100), 14, vv[j] > 100 ? rgb(0xff4a4a) : rgb(0xffd23a)); gx::rrectStroke(bx, by, 70, 14, 0, 1, rgb(0xffffff)); T(lb[j], bx - 8, by + 12, 12); }
            T("↻", 40, y0 + 100, 20); T("✔", CW - 50, y0 + 100, 20, rgb(0x9bff9b));
            if (k.st == 0) T(k.side == 0 ? "жарится A" : "жарится B", CW / 2 - 30, y0 + 100, 12);
        }
        for (auto& f : fx) { f.t -= dt; FX(f.s, f.x, f.y, 28, f.c, f.t * 2); }
        fx.erase(std::remove_if(fx.begin(), fx.end(), [](const Fx& f) { return f.t <= 0; }), fx.end());
        return t <= 0;
    }
    void down(Pt p) override {
        int i = (int)std::floor((p.y - 22) / 122); if (i < 0 || i > 3) return; Sk& k = sk[i]; if (k.st != 0) return;
        if (p.x < CW / 2) { k.side ^= 1; sfx::pop(); return; }
        float m = std::min(k.a, k.b), M = std::max(k.a, k.b); float fy = 22.f + i * 122 + 55;
        if (m >= 70 && M <= 100) { perf++; k.st = 2; k.wt = .8f; playClip("16"); fx.push_back({"ИДЕАЛЬНО! 🍖", CW / 2, fy, 1.2f, rgb(0x8dff6a)}); }
        else { k.st = 2; k.wt = .8f; sfx::nope(); fx.push_back({m < 70 ? "Сыровато…" : "Пересушил", CW / 2, fy, 1.f, rgb(0xffd23a)}); }
    }
    std::string info() override { return "⏱ " + std::to_string(std::max(0, (int)t)) + " · 🍖 " + std::to_string(perf) + " · 💥 " + std::to_string(burnt); }
    void result(std::string& a, std::string& b, int& c) override { a = "Идеальных: " + std::to_string(perf) + " · сгорело: " + std::to_string(burnt); b = ""; c = perf * 5; S.shashlik += perf; }
};

// ============================================================ ТУАЛЕТ
struct Toilet : Game {
    struct Dr { float x, y; bool done; }; struct Fx { std::string s; float x, y, t; Color c; };
    int tries, hits; float t, ax; std::vector<Dr> drops; std::vector<Fx> fx;
    const char* title() override { return "🚽 Попади в унитаз"; }
    std::string hint() override { return "Тапни — капля упадёт вниз. Попади в отверстие!"; }
    void init() override { tries = 6; hits = 0; t = 0; drops.clear(); fx.clear(); }
    bool tick(float dt) override {
        t += dt; ax = CW / 2 + std::sin(t * 2.4f) * 130; float hx = CW / 2 + std::sin(t * 1.5f + 1) * (100 + hits * 6), hw = 34 - hits * 1.5f;
        gx::rect(0, 0, CW, CH, rgb(0xcfe8f5)); gx::rect(0, CH - 70, CW, 70, rgb(0x7f9bb0)); E("🚰", ax, 60, 50);
        gx::ellipse(hx, CH - 62, hw + 18, 22, rgb(0xffffff)); gx::ellipse(hx, CH - 62, hw, 12, rgb(0x2a3b4a));
        for (auto& d : drops) { d.y += 460 * dt; E("💧", d.x, d.y, 26);
            if (d.y >= CH - 64 && !d.done) { d.done = true;
                if (std::fabs(d.x - hx) < hw) { hits++; fx.push_back({"ПОПАЛ!", hx, CH - 100, .7f, rgb(0x6dff8d)}); sfx::pop(); } else { fx.push_back({"мимо", d.x, CH - 100, .7f, rgb(0xff8a8a)}); sfx::nope(); } } }
        drops.erase(std::remove_if(drops.begin(), drops.end(), [](const Dr& d) { return d.done; }), drops.end());
        for (auto& f : fx) { f.t -= dt; FX(f.s, f.x, f.y - (.7f - f.t) * 40, 22, f.c, f.t * 1.6f); }
        fx.erase(std::remove_if(fx.begin(), fx.end(), [](const Fx& f) { return f.t <= 0; }), fx.end());
        return tries <= 0 && drops.empty() && fx.empty();
    }
    void down(Pt) override { if (tries > 0) { tries--; drops.push_back({ax, 90, false}); } }
    std::string info() override { return "💧 " + std::to_string(tries) + "  ·  ✔ " + std::to_string(hits); }
    void result(std::string& a, std::string& b, int& c) override { a = "Попаданий: " + std::to_string(hits) + " из 6"; b = ""; c = hits * 2; S.wc = clampf(S.wc + 20 + hits * 14, 0, 100); }
};

// ============================================================ СТРИЖКА
struct Cut : Game {
    static constexpr float MX = 10, MY = 70;
    std::vector<Pt> hair; struct Cell { float x, y; bool cut; }; std::vector<Cell> cells;
    struct P { float x, y, vx, vy, r, life; }; std::vector<P> fx;
    float t, slipCd, shake, talk, talkIn, buzz, doneT, intro, prog; int slips; bool done, hasPrev; Pt prev; Color stub = rgb(0x463a34);
    static bool inPoly(const std::vector<Pt>& p, float x, float y) { bool c = false; for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) if (((p[i].y > y) != (p[j].y > y)) && (x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x)) c = !c; return c; }
    static bool inFace(Pt p) { float dx = (p.x - (166 + MX)) / 80, dy = (p.y - (205 + MY)) / 108; return dx * dx + dy * dy < 1; }
    Cut() {
        static const float H[20][2] = {{90, 172}, {84, 120}, {86, 95}, {98, 72}, {122, 56}, {155, 48}, {190, 50}, {222, 60}, {243, 85}, {249, 115}, {247, 150}, {238, 168}, {236, 130}, {222, 119}, {200, 112}, {165, 111}, {130, 108}, {108, 118}, {100, 150}, {96, 172}};
        for (auto& h : H) hair.push_back({h[0] + MX, h[1] + MY});
        for (float y = 40 + MY; y < 160 + MY; y += 6) for (float x = 80 + MX; x < 255 + MX; x += 6) if (inPoly(hair, x, y)) cells.push_back({x, y, false});
        if (const Image* m = asset_image("man")) {       // цвет щетины из тёмной области волос
            double r = 0, g = 0, b = 0; int n = 0;
            for (int yy = 112; yy < 134; yy++) for (int xx = 150; xx < 190; xx++) { const uint8_t* q = &m->px[((size_t)yy * m->w + xx) * 4]; r += q[0]; g += q[1]; b += q[2]; n++; }
            if (n) stub = rgb(((int)std::min(255.0, r / n * .26 + 14) << 16) | ((int)std::min(255.0, g / n * .26 + 17) << 8) | (int)std::min(255.0, b / n * .26 + 16));
        }
    }
    const char* title() override { return "✂️ Стрижка"; }
    std::string hint() override { return "Води машинкой по волосам. Лицо не трогай!"; }
    void init() override { for (auto& c : cells) c.cut = false; rcStop(); rcPlay(31); t = 0; fx.clear(); slips = 0; slipCd = shake = talk = buzz = doneT = 0; talkIn = rnd(4, 9); hasPrev = false; done = false; intro = rcDur(31); prog = 0; }
    int paint(float x, float y) { int f = 0; for (auto& k : cells) if (!k.cut && (k.x - x) * (k.x - x) + (k.y - y) * (k.y - y) < 225) { k.cut = true; f++; } return f; }
    int stroke(Pt a, Pt b) { int n = std::max(1, (int)std::ceil(std::hypot(b.x - a.x, b.y - a.y) / 6)), f = 0; for (int i = 1; i <= n; i++) f += paint(a.x + (b.x - a.x) * i / n, a.y + (b.y - a.y) * i / n); return f; }
    bool tick(float dt) override {
        t += dt; slipCd -= dt; shake = std::max(0.f, shake - dt); talk = std::max(0.f, talk - dt); intro -= dt; buzz -= dt;
        if (!done && intro <= -1) { talkIn -= dt; if (talkIn <= 0 && talk <= 0) { rcPlay(32); talk = rcDur(32) + .2f; talkIn = rnd(10, 18); } }
        if (gDown && !done && dt > 0) {
            Pt p = gPtr;
            if (inPoly(hair, p.x, p.y) || (hasPrev && !inFace(p))) {
                int fresh = stroke(hasPrev ? prev : p, p);
                for (int i = 0; i < std::min(3, fresh / 4 + (fresh > 0)); i++) fx.push_back({p.x + rnd(-10, 10), p.y + rnd(-6, 6), rnd(-40, 40), rnd(-60, -10), rnd(0, 6), .9f});
                if (buzz <= 0) { au::tone(95 + frand() * 20, .07f, 2, .035f); buzz = .06f; }
            } else if (inFace(p) && slipCd <= 0) { slips++; slipCd = .9f; shake = .35f; sfx::nope(); }
            prev = p; hasPrev = true;
        } else hasPrev = false;
        int cc = 0; for (auto& k : cells) cc += k.cut; prog = cells.empty() ? 0 : (float)cc / cells.size();
        if (!done && prog >= .93f) { done = true; rcStop(); sfx::win(); }
        if (done) doneT += dt;
        vgr(0, 0, CW, CH, 0x2a3b3a, 0x14201f);
        float sx = shake > 0 ? std::sin(t * 60) * 4 : 0;
        gx::save(); gx::translate(sx, 0);
        if (const Image* m = asset_image("man")) gx::image(m, MX, MY, (float)m->w, (float)m->h);
        gx::save(); gx::clipPoly(hair.data(), (int)hair.size()); for (auto& k : cells) if (k.cut) gx::ellipse(k.x, k.y, 5.5f, 5.5f, stub); gx::restore();
        gx::restore();
        if (slipCd > .45f) T("Ай! 😖", 166 + MX + sx, 205 + MY - 10, 28, rgb(0xffd23a));
        for (auto& f : fx) { f.life -= dt; f.vy += 420 * dt; f.x += f.vx * dt; f.y += f.vy * dt; f.r += dt * 5; gx::save(); gx::translate(f.x, f.y); gx::rotate(f.r * 57.3f); gx::setAlpha(std::min(1.f, f.life)); gx::rect(-1.5f, -4, 3, 8, rgb(0x0d1513)); gx::restore(); }
        fx.erase(std::remove_if(fx.begin(), fx.end(), [](const P& f) { return f.life <= 0 || f.y > CH + 10; }), fx.end());
        if (fx.size() > 260) fx.erase(fx.begin(), fx.begin() + (fx.size() - 260));
        if (gDown && !done) E("✂️", gPtr.x + std::sin(t * 90) * 1.5f + 14, gPtr.y - 14, 40);
        gx::rrect(20, CH - 30, CW - 40, 12, 6, rgba8(0x000000, 0x88)); gx::rrect(20, CH - 30, std::max(0.f, (CW - 40) * std::min(1.f, prog / .93f)), 12, 6, rgb(0x74e39a));
        bool talking = talk > 0, introTalk = intro > 0; float bob = (talking || introTalk) ? std::sin(t * 18) * 2 : 0;
        if (headImage()) gx::image(headImage(), 14, CH - 132 + bob, 62, 68);
        if (talking || introTalk) { gx::rrect(80, CH - 122, talking ? 150.f : 130.f, 34, 14, rgb(0xffffff)); std::string dots(1 + ((int)(t * 4)) % 3, '.'); T("💬 " + dots, talking ? 155.f : 145.f, CH - 99, 20, rgb(0x222222)); }
        if (done) FX("Готово! 💈", CW / 2, MY - 18, 30, rgb(0x8dff6a), doneT * 1.2f);
        return done && doneT > 1.6f;
    }
    void down(Pt) override {} std::string info() override { return "💈 " + std::to_string(std::min(100, (int)std::lround(prog / .93f * 100))) + "%" + (slips ? "  ·  🩹 " + std::to_string(slips) : ""); }
    void result(std::string& a, std::string& b, int& c) override { a = "Причёска готова! 💈"; b = slips ? "Порезов: " + std::to_string(slips) : "Без единого пореза"; c = std::max(5, 30 - slips * 4); S.energy = clampf(S.energy - 3, 0, 100); }
    void onClose() override { rcStop(); }
};

// ============================================================ РАЗВЛЕКИ НАДЮХУ
struct Nad : Game {
    struct Wish { const char* e; }; struct Hrt { float x, y, vx, vy, life, s; };
    static const char* W[5];
    float t, mood, wt, wl, mad, endT, blinkT, sayT; int combo, good, wish, end; std::string say; std::vector<Hrt> hearts; std::vector<int> bag;
    const char* title() override { return "💞 Развлеки Надюху"; }
    std::string hint() override { return "Угадай, чего хочет Надюха, и жми нужную кнопку!"; }
    std::vector<std::string> acts() override { return {"💐", "🍫", "💃", "😘", "🎵"}; }
    void init() override { rcStop(); bag.clear(); t = 0; mood = 30; combo = good = 0; wish = -1; wt = .8f; wl = 0; mad = 0; hearts.clear(); end = 0; endT = 0; blinkT = 2; sayT = 0; say = ""; }
    int nextVoice() { if (bag.empty()) { bag = {33, 34, 35, 36, 37}; for (int i = 4; i > 0; i--) std::swap(bag[i], bag[irand(i + 1)]); } int v = bag.back(); bag.pop_back(); return v; }
    void act(int k) override {
        if (end) return;
        if (wish == k) { mood = std::min(100.f, mood + 13 + std::min(combo, 5) * 2); combo++; good++; wish = -1; wt = .55f; mad = 0; say = pickS({"Ой, как мило! 💕", "Ты душка!", "Ещё хочу!", "Мур 😘"}); sayT = 1.1f;
            for (int i = 0; i < 7; i++) hearts.push_back({rnd(215, 300), rnd(250, 320), rnd(-20, 20), rnd(-70, -35), 1.6f, rnd(18, 32)}); sfx::pop(); rcPlay(nextVoice()); }
        else { mood = std::max(0.f, mood - 8); combo = 0; mad = .9f; say = wish >= 0 ? "Не то! 😤" : "Рано! 😒"; sayT = 1; sfx::nope(); }
    }
    void nadya(float x, float y, float m, bool blink, bool angry) {
        Color hair = rgb(0x6b3a22);
        gx::ellipse(x, y + 14, 50, 64, hair);
        { std::vector<Pt> v; for (int i = 0; i <= 16; i++) { float a = PI_ + i * PI_ / 16; v.push_back({x + 60 * std::cos(a), y + 118 + 46 * std::sin(a)}); } v.push_back({x + 60, y + 170}); v.push_back({x - 60, y + 170}); gx::poly(v.data(), (int)v.size(), rgb(0xff6b9a)); }
        gx::ellipse(x - 20, y + 130, 4, 4, rgba8(0xffffff, 0x55)); gx::ellipse(x + 15, y + 142, 4, 4, rgba8(0xffffff, 0x55)); gx::ellipse(x + 28, y + 124, 4, 4, rgba8(0xffffff, 0x55));
        gx::rect(x - 9, y + 30, 18, 22, rgb(0xf4c6a4)); gx::ellipse(x, y, 42, 42, rgb(0xf4c6a4));
        { std::vector<Pt> v{{x - 44, y - 4}}; quad(v, {x - 44, y - 4}, {x - 40, y - 50}, {x, y - 48}); quad(v, {x, y - 48}, {x + 40, y - 50}, {x + 44, y - 4}); quad(v, {x + 44, y - 4}, {x + 22, y - 26}, {x - 4, y - 24}); quad(v, {x - 4, y - 24}, {x - 28, y - 22}, {x - 44, y - 4}); gx::poly(v.data(), (int)v.size(), hair); }
        gx::ellipse(x + 30, y - 40, 9, 9, rgb(0xffd23a)); gx::ellipse(x + 44, y - 34, 7, 7, rgb(0xffd23a));
        Color bl = rgb(0xff5a6e, .15f + m / 100 * .5f); gx::ellipse(x - 26, y + 12, 9, 5.5f, bl); gx::ellipse(x + 26, y + 12, 9, 5.5f, bl);
        Color br = rgb(0x4a2a18); float tilt = angry ? -4 : (m > 60 ? 2 : 0);
        gx::line(x - 22, y - 8 + tilt, x - 8, y - 10 - tilt, 2.4f, br); gx::line(x + 8, y - 10 - tilt, x + 22, y - 8 + tilt, 2.4f, br);
        if (blink) { gx::line(x - 20, y + 1, x - 8, y + 1, 2.4f, br); gx::line(x + 8, y + 1, x + 20, y + 1, 2.4f, br); }
        else if (m > 82) { gx::arc(x - 14, y + 3, 6, PI_ * 1.1f, PI_ * 1.9f, 2.4f, br); gx::arc(x + 14, y + 3, 6, PI_ * 1.1f, PI_ * 1.9f, 2.4f, br); }
        else { gx::ellipse(x - 14, y + 1, 4.6f, 4.6f, rgb(0x2a1a12)); gx::ellipse(x + 14, y + 1, 4.6f, 4.6f, rgb(0x2a1a12)); gx::ellipse(x - 12.6f, y - .4f, 1.4f, 1.4f, rgb(0xffffff)); gx::ellipse(x + 15.4f, y - .4f, 1.4f, 1.4f, rgb(0xffffff)); }
        Color mc = rgb(0xa8324a); std::vector<Pt> v;
        if (angry || m < 22) { v = {{x - 9, y + 27}}; quad(v, {x - 9, y + 27}, {x, y + 19}, {x + 9, y + 27}); gx::polyStroke(v.data(), (int)v.size(), false, 2.6f, mc); }
        else if (m < 55) gx::line(x - 9, y + 23, x + 9, y + 23, 2.6f, mc);
        else if (m < 82) { v = {{x - 11, y + 21}}; quad(v, {x - 11, y + 21}, {x, y + 33}, {x + 11, y + 21}); gx::polyStroke(v.data(), (int)v.size(), false, 2.6f, mc); }
        else { v = {{x - 12, y + 20}}; quad(v, {x - 12, y + 20}, {x, y + 40}, {x + 12, y + 20}); gx::poly(v.data(), (int)v.size(), mc); }
    }
    bool tick(float dt) override {
        t += dt; mad = std::max(0.f, mad - dt); sayT = std::max(0.f, sayT - dt); blinkT -= dt;
        if (!end) {
            mood = std::max(0.f, mood - 3.4f * dt);
            if (wish < 0) { wt -= dt; if (wt <= 0) { wish = irand(5); wl = 4.4f - std::min(1.6f, good * .12f); } }
            else { wl -= dt; if (wl <= 0) { mood = std::max(0.f, mood - 9); combo = 0; wish = -1; wt = .6f; mad = .9f; say = "Скучно… 😒"; sayT = 1; } }
            if (mood >= 96) { mood = 100; end = 1; endT = 0; wish = -1; rcStop(); sfx::win(); } else if (mood <= 0) { end = 2; endT = 0; wish = -1; sfx::lose(); }
        } else endT += dt;
        float m = mood; bool kiss = end == 1;
        vgr(0, 0, CW, CH, 0x3a1d5c, 0xc9567f);
        for (int i = 0; i < 16; i++) gx::rect((float)((i * 83) % (int)CW), (float)((i * 47) % 200), 2, 2, rgba8(0xffffff, 0x30));
        E("🕯️", 34, 215, 44); E("🕯️", 326, 215, 44); E("🌹", 180, 420, 30); gx::rect(0, CH - 150, CW, 150, rgb(0x4a1f3d));
        float k = kiss ? 1.f : m / 100, RX = 100 + k * (kiss ? 40 : 42), NX = 262 - k * (kiss ? 40 : 42), yy = 290, sway = std::sin(t * (2 + m / 30)) * (m > 50 ? 3.f : 1.f);
        if (shirtImage()) gx::image(shirtImage(), RX - 58, yy + 26, 116, 108);
        if (headImage()) gx::image(headImage(), RX - 42, yy - 52 + sway, 84, 91);
        bool blink = false; if (blinkT < 0) { blink = true; if (blinkT < -.15f) blinkT = rnd(2, 4); }
        nadya(NX, yy + sway * .6f, m, blink, mad > 0);
        if (kiss) E("💋", (RX + NX) / 2 + 4, yy + 8, 34);
        if (wish >= 0) {
            float bx = NX, by = yy - 92; gx::ellipse(bx, by, 32, 32, rgb(0xffffff)); Pt tri[3] = {{bx - 8, by + 28}, {bx - 2, by + 46}, {bx + 10, by + 30}}; gx::poly(tri, 3, rgb(0xffffff));
            E(W[wish], bx, by + 2, 38); float lf = std::max(0.f, wl / 4.4f); gx::arc(bx, by, 36, -PI_ / 2, -PI_ / 2 + PI_ * 2 * lf, 5, lf < .3f ? rgb(0xff5a5a) : rgb(0x74e39a));
        }
        if (sayT > 0 && !kiss) T(say, NX, yy - 150, 20, rgb(0xffd23a));
        for (auto& h : hearts) { h.life -= dt; h.x += h.vx * dt; h.y += h.vy * dt; gx::save(); gx::setAlpha(std::min(1.f, h.life)); E("💗", h.x, h.y, h.s); gx::restore(); }
        hearts.erase(std::remove_if(hearts.begin(), hearts.end(), [](const Hrt& h) { return h.life <= 0; }), hearts.end());
        if (m > 70 && !end && frand() < dt * 3) hearts.push_back({(RX + NX) / 2 + rnd(-20, 20), yy - 30, rnd(-15, 15), -45, 1.8f, rnd(16, 26)});
        if (kiss && frand() < dt * 14) hearts.push_back({rnd(80, 290), rnd(380, 440), rnd(-25, 25), rnd(-110, -50), 2, rnd(16, 28)});
        gx::rrect(20, 12, CW - 40, 18, 9, rgba8(0x000000, 0x99));
        { std::vector<Pt> pp; float w = std::max(0.f, (CW - 40) * m / 100); if (w > 2) { gx::save(); gx::clipRect(20, 12, w, 18); Color c[2] = {rgb(0xff9f43), rgb(0xff4f9a)}; float o[2] = {0, 1}; gx::grad(20, 12, CW - 40, 18, false, c, o, 2); gx::restore(); } }
        T("💗 Настроение Надюхи", CW / 2, 26, 13);
        if (combo > 1) T("Комбо ×" + std::to_string(combo), CW / 2, 52, 16, rgb(0xffd23a));
        if (kiss && endT > 1.8f) { gx::save(); gx::setAlpha(std::min(1.f, (endT - 1.8f) / 1.2f)); gx::rect(0, 0, CW, CH, rgb(0x000000)); T("💞 Занавес…", CW / 2, CH / 2, 30); gx::restore(); }
        return (kiss && endT > 3.6f) || (end == 2 && endT > 1.2f);
    }
    std::string info() override { return "💗 " + std::to_string((int)std::lround(mood)) + "%"; }
    void result(std::string& a, std::string& b, int& c) override {
        if (end == 1) { a = "Надюха счастлива! 💞"; c = 20 + good; } else { a = "Надюха заскучала 😒"; c = 3; }
        b = "Угадано желаний: " + std::to_string(good); S.energy = clampf(S.energy - 4, 0, 100);
    }
    void onClose() override { rcStop(); }
};
const char* Nad::W[5] = {"💐", "🍫", "💃", "😘", "🎵"};

// ============================================================ АРКАДА
struct Arcade : Game {
    float v, dir; bool done; std::string res;
    const char* title() override { return "🎰 Аркада множителей"; }
    std::string hint() override { return "Поймай момент остановки шкалы. Монеты не ставятся и не теряются."; }
    bool modal() override { return true; }
    void init() override { v = 0; dir = 1; done = false; res = ""; extraSound(28); }
    bool tick(float dt) override {
        gx::rect(0, 0, CW, CH, rgb(0x17233b));
        if (!done) { v += dir * 2.1f * 22.2f * dt; if (v >= 100) { v = 100; dir = -1; } if (v <= 0) { v = 0; dir = 1; } }
        T("Остановить шкалу ближе к 100%", CW / 2, 100, 16);
        gx::rrect(30, 150, CW - 60, 28, 14, rgba8(0xffffff, 0x25)); if (v > 1) gx::rrect(30, 150, (CW - 60) * v / 100, 28, 14, rgb(0x74e39a));
        char b[32]; snprintf(b, sizeof b, "Множитель: ×%.1f", 1 + v / 100 * 4); T(b, CW / 2, 230, 22);
        drawBtn({60, 280, CW - 120, 44, "Стоп и получить награду"}, done);
        T(res, CW / 2, 380, 18, rgb(0xffd23a));
        return false;
    }
    void down(Pt p) override {
        if (done || !inR(p, 60, 280, CW - 120, 44)) return;
        done = true; float m = 1 + std::max(0.f, 1 - std::fabs(v - 85) / 85) * 4; int gain = (int)std::lround(20 * m); addReward(gain);
        res = "Точность " + std::to_string((int)std::lround(v)) + "% · награда +" + std::to_string(gain) + " 🪙";
    }
    std::string info() override { return "🪙 " + std::to_string(S.coins); }
};

// ============================================================ ДУРАК
struct Durak : Game {
    struct Card { std::string r, s; };
    std::vector<Card> deck, hands[4]; std::string trump, status; bool ended; int turn; bool hasTable; Card ta, tb; bool hasTb;
    const char* title() override { return "🃏 Дурак — игра с ботами"; }
    std::string hint() override { return "Упрощённая партия против трёх ботов. Карты раздаются автоматически."; }
    bool modal() override { return true; }
    static int rv(const std::string& r) { static const char* R[9] = {"6", "7", "8", "9", "10", "В", "Д", "К", "Т"}; for (int i = 0; i < 9; i++) if (r == R[i]) return i; return -1; }
    bool beats(const Card& a, const Card& b) { return (a.s == b.s && rv(a.r) > rv(b.r)) || (a.s == trump && b.s != trump); }
    void init() override {
        const char* R[9] = {"6", "7", "8", "9", "10", "В", "Д", "К", "Т"}; const char* Su[4] = {"♠", "♥", "♦", "♣"};
        deck.clear(); for (auto s : Su) for (auto r : R) deck.push_back({r, s});
        for (int i = (int)deck.size() - 1; i > 0; i--) std::swap(deck[i], deck[irand(i + 1)]);
        for (auto& h : hands) { h.assign(deck.begin(), deck.begin() + 6); deck.erase(deck.begin(), deck.begin() + 6); }
        trump = deck.empty() ? "♠" : deck.back().s; turn = 0; hasTable = false; hasTb = false; ended = false; status = "Твой ход: выбери карту для атаки.";
    }
    void draw0() { while (hands[0].size() < 6 && !deck.empty()) { hands[0].push_back(deck.front()); deck.erase(deck.begin()); } }
    bool finishCheck() {
        int alive = 0; bool allEmpty = true; for (auto& h : hands) { if (!h.empty()) alive++; allEmpty &= h.empty(); }
        if (alive <= 1 || (deck.empty() && allEmpty)) { ended = true; if (hands[0].empty()) { status = "Победа! Ромчик вышел первым."; extraSound(21); addReward(30); } else { status = "Партия окончена. Попробуй ещё раз!"; extraSound(22); } return true; }
        return false;
    }
    bool tick(float) override {
        gx::rect(0, 0, CW, CH, rgb(0x17233b));
        T("Козырь: " + trump + " · В колоде: " + std::to_string(deck.size()), CW / 2, 70, 16);
        T(status, CW / 2, 105, 14, rgb(0xffd23a));
        std::string tt = hasTable ? "Атака: " + ta.r + ta.s + " · Защита: " + (hasTb ? tb.r + tb.s : std::string("нет")) : "Стол пуст"; T(tt, CW / 2, 150, 16);
        float x = 14, y = 200; for (size_t i = 0; i < hands[0].size(); i++) {
            if (x + 48 > CW - 10) { x = 14; y += 62; }
            bool dis = ended || turn != 0 || hasTable; bool red = hands[0][i].s == "♥" || hands[0][i].s == "♦";
            gx::rrect(x, y, 48, 54, 8, rgb(0xffffff, dis ? .55f : 1.f)); T(hands[0][i].r + hands[0][i].s, x + 24, y + 33, 16, red ? rgb(0xd0202a) : rgb(0x222222)); x += 54;
        }
        drawBtn({20, 430, 190, 40, "Взять / закончить ход", false}); drawBtn({220, 430, 120, 40, "Позвать друзей", false});
        return false;
    }
    void down(Pt p) override {
        if (inR(p, 220, 430, 120, 40)) { os::setClipboard("Заходи в «Ромчик красавчик» — сыграем в Дурака!"); say("Текст приглашения скопирован!", 1800); return; }
        if (inR(p, 20, 430, 190, 40)) {
            if (ended) return;
            if (hasTable) { hands[0].push_back(ta); if (hasTb) hands[0].push_back(tb); hasTable = false; status = "Ты забрал карты. Ход переходит дальше."; draw0(); finishCheck(); } else status = "Выбери карту для атаки.";
            return;
        }
        if (ended || turn != 0 || hasTable) return;
        float x = 14, y = 200; for (size_t i = 0; i < hands[0].size(); i++) {
            if (x + 48 > CW - 10) { x = 14; y += 62; }
            if (inR(p, x, y, 48, 54)) { playerPlay((int)i); return; } x += 54;
        }
    }
    void playerPlay(int i) {
        ta = hands[0][i]; hands[0].erase(hands[0].begin() + i); hasTable = true; hasTb = false; status = "Бот защищается…";
        after(.65f, [this]() {
            if (!hasTable) return;
            auto& d = hands[1]; int idx = -1; for (size_t k = 0; k < d.size(); k++) if (beats(d[k], ta)) { idx = (int)k; break; }
            if (idx >= 0) { tb = d[idx]; d.erase(d.begin() + idx); hasTb = true; status = "Бот отбился картой " + tb.r + tb.s + "."; }
            else { d.push_back(ta); status = "Бот взял карту. Твой следующий ход."; }
            draw0(); turn = 0; hasTable = false; finishCheck();
        });
    }
    std::string info() override { return "🪙 " + std::to_string(S.coins); }
};

// ============================================================ ЗНАКОМСТВА
struct Dating : Game {
    int idx = 0; std::string status = "Посмотри анкету и выбери реакцию."; double nextAt = 0;
    const char* title() override { return "💘 Знакомства"; }
    std::string hint() override { return "Используются только предоставленные фотографии."; }
    bool modal() override { return true; }
    void init() override { status = "Посмотри анкету и выбери реакцию."; nextAt = 0; }
    bool tick(float) override {
        if (nextAt > 0 && gT >= nextAt) { nextAt = 0; status = "Посмотри анкету и выбери реакцию."; }
        gx::rect(0, 0, CW, CH, rgb(0x17233b));
        const Image* im = girlPhoto(idx);
        if (im) { float dw = 116 * 1.6f, dh = 140 * 1.6f, sd = std::min((float)im->w, im->h * dw / dh); float sw = sd, sh = sd * dh / dw; gx::save(); gx::clipRect(20, 60, dw, dh); gx::image(im, 20, 60, dw, dh, 1, (im->w - sw) / 2, (im->h - sh) / 2, sw, sh); gx::restore(); }
        Tl(girlName(idx), 220, 100, 24, rgb(0xffffff)); Tl("Любит общение, прогулки", 220, 135, 13, rgb(0xc8d3e6)); Tl("и хорошее настроение.", 220, 153, 13, rgb(0xc8d3e6));
        T(status, CW / 2, 330, 15);
        Btn n{30, 380, 140, 44, "✖ Пропустить", false}, l{190, 380, 140, 44, "♥ Нравится", true}; drawBtn(n); drawBtn(l);
        return false;
    }
    void down(Pt p) override {
        if (nextAt > 0) return;
        if (inR(p, 190, 380, 140, 44)) { extraSound(23); status = "Симпатия отправлена!"; idx++; nextAt = gT + .8; }
        else if (inR(p, 30, 380, 140, 44)) { extraSound(24); status = "Анкета пропущена."; idx++; nextAt = gT + .8; }
    }
    std::string info() override { return ""; }
};

// ============================================================ ПОГОНЯ
struct Chase : Game {
    struct O { int lane; float y; }; float t, spawn; int lane; std::vector<O> obs; bool ended; std::string status;
    const char* title() override { return "🚔 Уход от погони"; }
    std::string hint() override { return "Стрелки ← → или тап слева/справа. Уклоняйся 15 секунд."; }
    bool modal() override { return true; }
    void init() override { t = 15; spawn = 0; lane = 1; obs.clear(); ended = false; status = "Полицейские заметили компанию!"; extraSound(25); extraSound(27); }
    void fin(bool win) { if (ended) return; ended = true; if (win) { addReward(500); status = "Успешный побег! +500 🪙"; } else { addReward(-250); extraSound(26); status = "Поймали! −250 🪙"; } }
    bool tick(float dt) override {
        if (!ended) { dt = std::min(dt, .1f); t -= dt; spawn -= dt;
            if (spawn <= 0) { obs.push_back({irand(3), -20.f}); spawn = .65f + frand() * .35f; }
            for (auto& o : obs) o.y += 340 * dt;
            for (auto& o : obs) if (o.y > 440 && o.y < 500 && o.lane == lane) { fin(false); break; }
            if (!ended && t <= 0) fin(true); }
        gx::rect(0, 0, CW, CH, rgb(0x26334c));
        for (float y = 0; y < CH; y += 22) { gx::rect(119, y, 2, 12, rgba8(0xffffff, 0x55)); gx::rect(239, y, 2, 12, rgba8(0xffffff, 0x55)); }
        for (auto& o : obs) E("🚧", o.lane * 120 + 60.f, o.y, 40);
        E("🏃", lane * 120 + 60.f, 480, 46);
        T(status, CW / 2, 60, 15, rgb(0xffd23a));
        return false;
    }
    void mv(int d) { if (!ended) lane = std::max(0, std::min(2, lane + d)); }
    void down(Pt p) override { mv(p.x < CW / 2 ? -1 : 1); }
    void key(int c, bool dn) override { if (!dn) return; if (c == 1) mv(-1); if (c == 2) mv(1); }
    std::string info() override { return ended ? "" : "⏱ " + std::to_string(std::max(0, (int)std::ceil(t))); }
};

// ============================================================ оверлей
namespace {
std::unique_ptr<Game> games[12]; Game* cur = nullptr;
bool running = false, resOn = false; std::string r1, r2a, r2b; std::string r2got;
}
static Game* make(GameId id) {
    switch (id) {
    case G_SHOOT: return new Shoot; case G_CATCH: return new Catch; case G_SHEEP: return new Sheep; case G_GIRLS: return new Girls;
    case G_GRILL: return new Grill; case G_TOILET: return new Toilet; case G_CUT: return new Cut; case G_NAD: return new Nad;
    case G_ARCADE: return new Arcade; case G_DURAK: return new Durak; case G_DATING: return new Dating; default: return new Chase;
    }
}
void overlayOpen(GameId id) {
    if (!games[id]) games[id].reset(make(id));
    cur = games[id].get(); running = true; resOn = false; gDown = false; cur->init();
}
bool overlayActive() { return cur && running; }
void overlayFrame(float dt) { gDt = dt; }
static void closeOverlay() { if (cur) cur->onClose(); running = false; resOn = false; cur = nullptr; gDown = false; }

static const Btn closeBtn{441 - 44, 62, 44, 26, "✖", false};
static Btn okBtn() { return {CW * SC / 2 + OX - 60, 450, 120, 46, "Ок", true}; }
void overlayDraw() {
    if (!cur || !running) return;
    gx::rect(0, 0, gx::W, gx::H, rgba8(0x000000, 0xdd));
    Tl(cur->title(), 9, 76, 14, rgb(0xffffff)); gx::text(cur->info(), gx::W / 2, 76, 14, rgb(0xffffff), 1, 1, 800);
    gx::rrect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, 8, rgba8(0xffffff, 0x33)); gx::text("✖", closeBtn.x + 22, closeBtn.y + 13, 14, rgb(0xffffff), 1, 1, 800);
    gx::rrect(OX, OY, CW * SC, CH * SC, 13.5f, rgb(0x1b2340));
    gx::save(); gx::translate(OX, OY); gx::scale(SC, SC); gx::clipRect(0, 0, CW, CH);
    bool finished = cur->tick(resOn ? 0.f : gDt);
    gx::restore();
    { std::string h = cur->hint(), line; float yy = OY + CH * SC + 14; size_t i = 0;       // перенос длинной подсказки
      while (i < h.size()) { size_t j = h.find(' ', i); if (j == std::string::npos) j = h.size(); std::string w = h.substr(i, j - i);
          if (!line.empty() && gx::textW(line + " " + w, 12, 700) > gx::W - 20) { gx::text(line, gx::W / 2, yy, 12, rgb(0xcfd6e6), 1, 1, 700); yy += 15; line = w; } else line += (line.empty() ? "" : " ") + w;
          i = j + 1; }
      if (!line.empty()) gx::text(line, gx::W / 2, yy, 12, rgb(0xcfd6e6), 1, 1, 700); }
    auto a = cur->acts();
    if (!a.empty() && !resOn) for (size_t i = 0; i < a.size(); i++) { float bw = 52, gap = 6, tot = a.size() * bw + (a.size() - 1) * gap, x = (gx::W - tot) / 2 + i * (bw + gap); gx::rrect(x, OY + CH * SC + 44, bw, 40, 8, rgb(0xffffff)); gx::text(a[i], x + bw / 2, OY + CH * SC + 64, 26, rgb(0x222222), 1, 1); }
    if (finished && !resOn && !cur->modal()) {
        int c = 0; cur->result(r2a, r2b, c); r1 = r2a; r2a = r2b; int got = addCoins(c); r2got = "+" + std::to_string(got) + " 🪙" + (mutantOn() ? " (мутант ×2)" : "");
        sfx::win(); resOn = true; saveGame();
    }
    if (resOn) {
        gx::rrect(OX, 98, CW * SC, 590, 13.5f, rgba8(0x000000, 0xbb));
        float cy = 300; T(r1, gx::W / 2, cy, 24); cy += 36;
        if (!r2a.empty()) { T(r2a, gx::W / 2, cy, 16, rgb(0xffd23a)); cy += 26; }
        T(r2got, gx::W / 2, cy, 16, rgb(0xffd23a));
        Btn o = okBtn(); o.y = cy + 30; drawBtn(o);
    }
}
bool overlayDown(float x, float y) {
    if (!cur || !running) return false;
    if (inR({x, y}, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) { closeOverlay(); return true; }
    if (resOn) { Btn o = okBtn(); float cy = 300 + 36 + (r2a.empty() ? 0 : 26); o.y = cy + 30; if (inR({x, y}, o.x, o.y, o.w, o.h)) closeOverlay(); return true; }
    auto a = cur->acts();
    if (!a.empty()) for (size_t i = 0; i < a.size(); i++) { float bw = 52, gap = 6, tot = a.size() * bw + (a.size() - 1) * gap, bx = (gx::W - tot) / 2 + i * (bw + gap); if (inR({x, y}, bx, OY + CH * SC + 44, bw, 40)) { cur->act((int)i); return true; } }
    Pt p{(x - OX) / SC, (y - OY) / SC};
    if (p.x >= 0 && p.x <= CW && p.y >= 0 && p.y <= CH) { gDown = true; gPtr = p; cur->down(p); }
    return true;
}
void overlayMove(float x, float y) { if (!cur || !running) return; gPtr = {(x - OX) / SC, (y - OY) / SC}; if (gDown) cur->move(gPtr); }
void overlayUp(float, float) { if (cur) cur->up(gPtr); gDown = false; }
void overlayKey(int code, bool down) { if (cur && running) cur->key(code, down); }
