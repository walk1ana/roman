// Рыбалка: сцена пруда, поклёвка, мини-игра «держи рыбу в зоне».
#include "internal.h"

static int st = 0;                 // 0 idle, 1 wait, 2 bite, 3 fight
static int timerId = 0, target = 0;
static bool holding = false, bang = false, mgOn = false;
static float bobX = 250, bobY = 352, bobTX = 250, bobTY = 352;
static int bobMode = 0;            // 0 плавает, 1 неподвижно, 2 клюёт
static float zy, zv, fy, ft, prog, nt, zh;
static const float BAR_H = 44 * 8.f - 5.4f;                  // 44vh минус рамка
static double catchUntil = 0; static float catchS = 0;
static std::string cE, cN, cC;

static void setBob(float x, float y, bool snap = false) { bobTX = x; bobTY = y; if (snap) { bobX = x; bobY = y; } }

void fishingReset() {
    cancelTimer(timerId); st = 0; holding = false; mgOn = false; bang = false; bobMode = 0; setBob(250, 352); catchUntil = 0;
}
int fishInvCount(int* worth) {
    int n = 0, w = 0; for (int i = 0; i < 5; i++) { n += S.fish[i]; w += S.fish[i] * FISH[i].v; }
    if (worth) *worth = w;
    return n;
}
void fishingItems(std::vector<Item>& out) {
    auto btn = [&](const std::string& l, Act a, bool big = false) { Item i; i.label = l; i.act = a; i.big = big; out.push_back(i); return &out.back(); };
    int worth; int n = fishInvCount(&worth);
    if (st == 0) {
        btn("🎣 Забросить", A_CAST, true);
        if (n) btn("💰 Продать улов (" + std::to_string(n) + " шт · " + std::to_string(worth) + " 🪙)", A_SELL);
        Item h; h.type = 2; h.full = true;
        h.label = "Поймано всего: " + std::to_string(S.caught) + " · рекорд: " + (S.best >= 0 ? FISH[S.best].n : "—"); out.push_back(h);
    } else if (st == 1) {
        Item h; h.type = 2; h.label = "Ждём поклёвку… 🤫"; out.push_back(h);
        btn("↩️ Смотать", A_PULL);
    } else if (st == 2) { Item* b = btn("ПОДСЕКАЙ!", A_HOOK, true); b->red = true; b->pulse = true; }
    else btn("⬆️ ДЕРЖИ (зажми)", A_HOLD, true);
}

static int pickFish() { int r = irand(100), acc = 0; for (int i = 0; i < 5; i++) { acc += FISH[i].w; if (r < acc) return i; } return 0; }

static void bite() {
    if (st != 1) return;
    st = 2; bobMode = 2; bang = true; sfx::bite(); say("Клюёт!!!", 1200, W_POND);
    timerId = after(1.5f, []() { if (st == 2) { fishingReset(); playClip("6"); say("Ушла… эх!", 2000, W_POND); } });
}
static void cast() {
    st = 1; sfx::splash(); say("Закидываю…", 1200, W_POND);
    float x = 200 + frand() * 90, y = 370 + frand() * 60;
    bobMode = 1; setBob(250, 200, true); setBob(x, y);
    after(.38f, []() { if (st == 1) { bobMode = 0; sfx::splash(); } });
    target = pickFish();
    timerId = after(1.8f + frand() * 4.5f, bite);
}
static void hook() {
    if (st != 2) return;
    cancelTimer(timerId); st = 3; bang = false; bobMode = 2; mgOn = true; say("Тяни, тяни!", 1500, W_POND);
    float h = BAR_H; zh = h * (mutantOn() ? .36f : .26f); zy = h - zh; zv = 0; fy = h * .5f; ft = h * .5f; prog = 30; nt = 0;
}
static void win() {
    int f = target; fishingReset(); playClip("5");
    S.fish[f]++; S.caught++; if (S.best < 0 || FISH[f].v >= FISH[S.best].v) S.best = f;
    cE = FISH[f].e; cN = f == 4 ? "Ой, ботинок… 😅" : std::string("Поймал: ") + FISH[f].n + "!";
    cC = "цена " + std::to_string(FISH[f].v) + " 🪙" + (FISH[f].h ? " · ешь на кухне (+" + std::to_string(FISH[f].h) + ")" : "");
    catchUntil = gT + 2.6;
    say(f == 3 ? "ЩУКА!!! Вот это да!" : f == 4 ? "Мусор достался…" : pickS({"Есть! Красавец!", "Ура, поймал!", "Рыбак от бога!"}), 2500, W_POND);
    saveGame();
}
static void fail() { fishingReset(); playClip("6"); say("Сорвалась! 😤", 2200, W_POND); }
void fishingSell() {
    int sum = 0; for (int i = 0; i < 5; i++) { sum += S.fish[i] * FISH[i].v; S.fish[i] = 0; }
    if (!sum) return;
    sum = addCoins(sum); sfx::coin(); say("Продал на " + std::to_string(sum) + " 🪙", 2000, W_POND);
}
void fishingAct(Act a, bool down) {
    if (a == A_HOLD) { holding = down; return; }
    if (!down) return;
    if (a == A_CAST && st == 0) cast();
    else if (a == A_PULL) { fishingReset(); say("Смотал удочку", 1500, W_POND); }
    else if (a == A_HOOK) hook();
}
void fishingKey(int code, bool down) {
    if (code != 0 || gRoom != R_POND) return;
    if (down) { if (st == 2) hook(); else holding = true; } else holding = false;
}

void fishingFrame(float dt) {
    float k = std::min(1.f, dt / .12f); bobX += (bobTX - bobX) * k; bobY += (bobTY - bobY) * k;
    catchS += clampf((gT < catchUntil ? 1.f : 0.f) - catchS, -dt / .25f, dt / .25f);
    if (st != 3 || !mgOn) return;
    float d = std::min(.05f, dt), h = BAR_H; const FishDef& f = FISH[target];
    float sp = f.sp * (h / 280);
    zv += (holding ? -1500.f : 1100.f) * d * (h / 280); zy += zv * d;
    if (zy < 0) { zy = 0; zv = 0; } if (zy > h - zh) { zy = h - zh; zv = -zv * .25f; }
    nt -= d; if (nt <= 0) { ft = frand() * (h - 34); nt = (.5f + frand() * 1.1f) / f.d; }
    fy += clampf(ft - fy, -sp * d, sp * d);
    float fc = fy + 17; bool inside = fc > zy && fc < zy + zh;
    prog = clampf(prog + (inside ? 26.f : -17.f * f.d) * d, 0, 100);
    if (inside && frand() < .2f) sfx::reel();
    if (prog >= 100) win(); else if (prog <= 0) fail();
}

// ------------------------------------------------------------ рисование
static void quadFlat(std::vector<Pt>& v, Pt a, Pt c, Pt b) {
    for (int i = 1; i <= 10; i++) { float u = i / 10.f, m = 1 - u; v.push_back({m * m * a.x + 2 * m * u * c.x + u * u * b.x, m * m * a.y + 2 * m * u * c.y + u * u * b.y}); }
}
static void vgr(float x, float y, float w, float h, uint32_t a, uint32_t b) { Color c[2] = {rgb(a), rgb(b)}; float o[2] = {0, 1}; gx::grad(x, y, w, h, true, c, o, 2); }

void drawFisher(bool) {
    Color skin = skinColor(); float t = (float)gT;
    gx::save(); gx::translate(40, 12);
    gx::rrect(52, 268, 22, 64, 10, rgb(0x3d5a80)); gx::rrect(86, 268, 22, 64, 10, rgb(0x3d5a80));
    gx::ellipse(60, 332, 17, 6, rgb(0x2b2b2b)); gx::ellipse(98, 332, 17, 6, rgb(0x2b2b2b));
    gx::rect(72, 176, 16, 16, skin);
    gx::save(); gx::translate(29, 206); gx::rotate(6); gx::translate(-29, -206); gx::rrect(22, 206, 14, 62, 7, skin); gx::restore();
    if (shirtImage()) gx::image(shirtImage(), 29, 184, 102, 94);
    gx::line(124, 224, 150, 222, 14, skin, true);
    if (headImage()) gx::image(headImage(), 49, 110, 62, 68);
    gx::line(136, 236, 250, 118, 4, rgb(0x6b3f1d), true);
    gx::ellipse(150, 222, 8, 8, skin);
    float dy = bobMode == 0 ? 3 * (.5f - .5f * std::cos(t * 2 * PI_ / 1.6f)) : bobMode == 2 ? 14 * (.5f - .5f * std::cos(t * 2 * PI_ / .25f)) : 0.f;
    gx::line(250, 118, bobX, bobY + dy, 1.2f, rgb(0xffffff), false);
    gx::save(); gx::translate(bobX - 250, bobY - 352 + dy);
    gx::ellipse(250, 352, 7, 7, rgb(0xff3b3b)); gx::rrect(243, 352, 14, 7, 3, rgb(0xffffff));
    gx::restore();
    gx::restore();
}

void fishingDrawScene() {
    float t = (float)gT;
    gx::save(); gx::translate(-25, 0); gx::scale(1.25f, 1.25f);
    vgr(0, 0, 400, 640, 0x6cc4ff, 0xd8f3ff);
    gx::ellipse(330, 70, 30, 30, rgb(0xfff6b0));
    gx::ellipse(90, 80, 40, 14, rgb(0xffffff, .9f)); gx::ellipse(120, 70, 28, 14, rgb(0xffffff, .9f)); gx::ellipse(260, 130, 36, 11, rgb(0xffffff, .9f));
    { std::vector<Pt> v{{0, 300}}; quadFlat(v, {0, 300}, {60, 240}, {120, 290}); quadFlat(v, {120, 290}, {180, 340}, {250, 280}); quadFlat(v, {250, 280}, {320, 220}, {400, 270}); v.push_back({400, 350}); v.push_back({0, 350}); gx::poly(v.data(), (int)v.size(), rgb(0x3a8f4a)); }
    const char* tr[4] = {"🌲", "🌲", "🌳", "🌲"}; float tx[4] = {20, 60, 290, 340}, ty[4] = {300, 305, 290, 298};
    for (int i = 0; i < 4; i++) gx::text(tr[i], tx[i] + 20, ty[i] - 14, 40, rgb(0xffffff), 1, 1);
    vgr(0, 340, 400, 300, 0x3aa0e6, 0x0d4f8f);
    float rp = 10 * (.5f - .5f * std::cos(t * 2 * PI_ / 3.f));
    const float rx[3] = {220, 300, 180}, ry[3] = {380, 430, 500};
    for (int i = 0; i < 3; i++) { std::vector<Pt> v{{rx[i] + rp, ry[i]}}; quadFlat(v, {rx[i] + rp, ry[i]}, {rx[i] + rp + 10, ry[i] - 5}, {rx[i] + rp + 20, ry[i]}); quadFlat(v, {rx[i] + rp + 20, ry[i]}, {rx[i] + rp + 30, ry[i] + 5}, {rx[i] + rp + 40, ry[i]}); gx::polyStroke(v.data(), (int)v.size(), false, 2, rgb(0xffffff, .5f)); }
    const char* fs[3] = {"🐟", "🐠", "🐟"}; float fyy[3] = {440, 520, 590}, dur[3] = {14, 19, 23}, off[3] = {0, 6, 12};
    gx::save(); gx::setAlpha(.55f);
    for (int i = 0; i < 3; i++) { float ph = std::fmod(t + off[i], dur[i]) / dur[i]; gx::text(fs[i], -80 + 600 * ph + 13, fyy[i] - 9, 26, rgb(0xffffff), 1, 1); }
    gx::restore();
    gx::rect(0, 332, 190, 16, rgb(0x9a6b3c)); gx::rect(0, 348, 190, 6, rgb(0x6e4825));
    for (int x : {40, 90, 140}) gx::line((float)x, 332, (float)x, 348, 2, rgb(0x6e4825), false);
    gx::rect(40, 348, 12, 70, rgb(0x6e4825)); gx::rect(150, 348, 12, 70, rgb(0x6e4825));
    drawFisher(true);
    gx::restore();
}

void fishingDrawOverlay() {
    // «ПОДСЕКАЙ»
    if (bang) {
        float sc = 1 + .08f * (.5f - .5f * std::cos((float)gT * 2 * PI_ / .3f));
        gx::save(); gx::translate(gx::W / 2, .22f * gx::H + 50); gx::scale(sc, sc);
        gx::text("❗", 0, 2.7f, 18 * VM, rgb(0xffffff), 1, 1, 900); gx::text("❗", 0, 0, 18 * VM, rgb(0xff3b3b), 1, 1, 900); gx::restore();
    }
    if (mgOn) {
        float bx = gx::W - 5.f / 100 * gx::W - (49.5f + 9 + 14), by = .15f * gx::H, bw = 49.5f, bh = 44 * 8.f;
        gx::rrect(bx, by, bw, bh, 9, rgba8(0x0a3b6a, 0xcc));
        gx::save(); gx::clipRect(bx + 2.7f, by + 2.7f, bw - 5.4f, bh - 5.4f);
        float ix = bx + 2.7f, iy = by + 2.7f, iw = bw - 5.4f;
        gx::rect(ix, iy + zy, iw, zh, rgba8(0x47e06a, 0xcc)); gx::rect(ix, iy + zy, iw, 3, rgb(0xb9ffca)); gx::rect(ix, iy + zy + zh - 3, iw, 3, rgb(0xb9ffca));
        gx::text(FISH[target].e, ix + iw / 2, iy + fy + 14, 22.5f, rgb(0xffffff), 1, 1);
        gx::restore();
        gx::rrectStroke(bx + 1.35f, by + 1.35f, bw - 2.7f, bh - 2.7f, 8, 2.7f, rgb(0xffffff));
        float px = bx + bw + 9, pw = 14;
        gx::rrect(px, by, pw, bh, 9, rgba8(0x000000, 0x88));
        float inH = bh - 4.5f, fh = inH * prog / 100.f;
        gx::save(); gx::clipRect(px + 2.25f, by + 2.25f, pw - 4.5f, inH); vgr(px + 2.25f, by + 2.25f + inH - fh, pw - 4.5f, fh, 0xffd23a, 0xff9f43); gx::restore();
        gx::rrectStroke(px + 1.1f, by + 1.1f, pw - 2.2f, bh - 2.2f, 8, 2.25f, rgb(0xffffff));
    }
    if (catchS > .02f) {
        float w = std::max(gx::textW(cN, 18, 800), gx::textW(cC, 13.5f, 800)) + 45, h = 138;
        gx::save(); gx::translate(gx::W / 2, .32f * gx::H); gx::scale(catchS, catchS);
        gx::rrect(-w / 2, -h / 2 + 4.5f, w, h, 18, rgba8(0x000000, 0x66)); gx::rrect(-w / 2, -h / 2, w, h, 18, rgb(0xffffff));
        gx::text(cE, 0, -h / 2 + 9 + 40, 16 * VM, rgb(0xffffff), 1, 1);
        gx::text(cN, 0, -h / 2 + 9 + 79 + 12, 18, rgb(0x222222), 1, 1, 800);
        gx::text(cC, 0, -h / 2 + 9 + 79 + 30, 13.5f, rgb(0x666666), 1, 1, 800);
        gx::restore();
    }
}
