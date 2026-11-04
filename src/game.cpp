// «Ромчик красавчик» — ядро игры: состояние, комнаты, персонаж, панель, бар/компания, ввод.
#include "internal.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <sstream>

// ============================================================ утилиты и состояние
double gT = 0;
static std::mt19937 rng{std::random_device{}()};
float frand() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng); }
float frand(float a, float b) { return a + frand() * (b - a); }
int irand(int n) { return n <= 0 ? 0 : std::min(n - 1, (int)(frand() * n)); }

const FishDef FISH[5] = {
    {"plotva", "🐟", "Плотва", 5, 12, 40, 90, 1.f},
    {"okun", "🐠", "Окунь", 10, 18, 28, 130, 1.2f},
    {"lesh", "🐡", "Лещ", 16, 24, 16, 170, 1.5f},
    {"shchuka", "🦈", "Щука-гигант", 45, 40, 6, 240, 2.f},
    {"boot", "🥾", "Старый ботинок", 1, 0, 10, 70, .8f}};

State S;
float gDrunk = 0;
int gRoom = R_LIVING;
bool gSleeping = false;
static bool gLightsOff = false;
static bool gCalled = false;
static bool gSmoking = false;
static bool gBushy = false;          // Ромчик «в кустах»
static double gAsked = 0;
static bool gSaveDirty = false;

bool mutantOn() { return S.mutant > os::nowMs(); }
void saveGame() { gSaveDirty = true; }
void bumpHud() {}
int addCoins(int n) { n = (int)std::lround(n * (mutantOn() ? 2 : 1)); S.coins += n; saveGame(); return n; }

static void doSave() {
    std::ostringstream o;
    o << "hunger=" << S.hunger << "\nenergy=" << S.energy << "\nwc=" << S.wc << "\ncoins=" << S.coins << "\n";
    for (int i = 0; i < 5; i++) o << "fish" << i << "=" << S.fish[i] << "\n";
    o << "caught=" << S.caught << "\nbest=" << S.best << "\nmutant=" << (long long)S.mutant << "\nshashlik=" << S.shashlik << "\nsel=" << S.sel << "\ngirls=";
    for (size_t i = 0; i < S.girls.size(); i++) o << (i ? "," : "") << S.girls[i];
    o << "\n";
    os::writeSave(o.str());
}
static void loadGame() {
    std::istringstream in(os::loadSave()); std::string line;
    while (std::getline(in, line)) {
        auto p = line.find('='); if (p == std::string::npos) continue;
        std::string k = line.substr(0, p), v = line.substr(p + 1);
        double d = atof(v.c_str());
        if (k == "hunger") S.hunger = (float)d; else if (k == "energy") S.energy = (float)d; else if (k == "wc") S.wc = (float)d;
        else if (k == "coins") S.coins = (int)d; else if (k == "caught") S.caught = (int)d; else if (k == "best") S.best = (int)d;
        else if (k == "mutant") S.mutant = d; else if (k == "shashlik") S.shashlik = (int)d; else if (k == "sel") S.sel = std::max(0, std::min(5, (int)d));
        else if (k.rfind("fish", 0) == 0 && k.size() == 5) { int i = k[4] - '0'; if (i >= 0 && i < 5) S.fish[i] = std::max(0, (int)d); }
        else if (k == "girls") { std::stringstream ss(v); std::string g; while (std::getline(ss, g, ',')) if (girlIndex(g) >= 0) S.girls.push_back(g); }
    }
    S.hunger = clampf(S.hunger, 0, 100); S.energy = clampf(S.energy, 0, 100); S.wc = clampf(S.wc, 0, 100);
}

// ------------------------------------------------------------ таймеры (аналог setTimeout)
namespace {
struct Timer { int id; double at; std::function<void()> fn; };
std::vector<Timer> timers; int timerSeq = 1;
}
int after(float sec, std::function<void()> fn) { timers.push_back({timerSeq, gT + sec, std::move(fn)}); return timerSeq++; }
void cancelTimer(int id) { for (size_t i = 0; i < timers.size(); i++) if (timers[i].id == id) { timers.erase(timers.begin() + i); return; } }
static void runTimers() {
    for (size_t i = 0; i < timers.size();) {
        if (timers[i].at <= gT) { auto f = std::move(timers[i].fn); timers.erase(timers.begin() + i); f(); i = 0; }
        else i++;
    }
}

// ------------------------------------------------------------ звуки
namespace sfx {
void splash() { au::tone(900, .25f, 2, .1f, 120); }
void bite() { au::tone(880, .1f, 1, .15f); au::tone(1100, .12f, 1, .15f, 0, .12f); }
void reel() { au::tone(300 + frand() * 60, .05f, 1, .04f); }
void win() { const float f[4] = {523, 659, 784, 1047}; for (int i = 0; i < 4; i++) au::tone(f[i], .18f, 3, .22f, 0, i * .11f); }
void lose() { au::tone(300, .4f, 2, .15f, 90); }
void coin() { au::tone(1200, .08f, 1, .12f); au::tone(1600, .18f, 1, .12f, 0, .08f); }
void pop() { au::tone(500, .1f, 0, .2f, 900); }
void nope() { au::tone(200, .2f, 1, .12f, 150); }
}
void extraSound(int n) { au::play(au::clip(std::to_string(n)), 100 + n); }

// ------------------------------------------------------------ пузыри речи
namespace {
struct Bub { std::string t; double until = 0; float s = 0; };
Bub bub[3];
std::map<std::string, Bub> crewBub;
bool has(const std::string& t, const char* sub) { return t.find(sub) != std::string::npos; }
}
void say(const std::string& t, float ms, Who w) {
    // реплики о потребностях сопровождаются голосом (как в оригинале)
    if (has(t, "туалет") || has(t, "терпеть") || has(t, "уборная")) extraSound(29);
    if (has(t, "рать хочу") || has(t, "окорми") || has(t, "ивот урчит") || has(t, "есть хочу")) extraSound(30);
    bub[w].t = t; bub[w].until = gT + ms / 1000.0;
}
static void csay(const std::string& id, const std::string& t, float ms = 2000) { Bub& b = crewBub[id]; b.t = t; b.until = gT + ms / 1000.0; }

// ------------------------------------------------------------ анимации персонажа
namespace {
int aCls = 0; double aStart = 0, aEnd = 0;       // 1 shake, 2 jump, 3 laugh, 4 dance
}
void anim(const char* cls, float ms) {
    std::string c = cls; aCls = c == "shake" ? 1 : c == "jump" ? 2 : c == "laugh" ? 3 : c == "dance" ? 4 : 0;
    aStart = gT; aEnd = gT + ms / 1000.0;
}

// ------------------------------------------------------------ голос и липсинк
static float gJaw = 0;
void playClip(const std::string& name, const char* cls) {
    const au::Clip* c = au::clip(name);
    if (!c) { if (cls) anim(cls, 2500); return; }
    au::play(c, 0);
    if (cls) anim(cls, c->dur() * 1000.f);
}

// ------------------------------------------------------------ картинки
static const Image *gHead, *gShirt, *gBottle;
static Image gHeadMut;
static void makeMutantHead() {
    if (!gHead) return;
    gHeadMut = *gHead; gHeadMut.tex = nullptr;
    const float a = 70.f * PI_ / 180.f, c = std::cos(a), s = std::sin(a), sat = 2.4f;
    float H[3][3] = {{.213f + c * .787f - s * .213f, .715f - c * .715f - s * .715f, .072f - c * .072f + s * .928f},
                     {.213f - c * .213f + s * .143f, .715f + c * .285f + s * .140f, .072f - c * .072f - s * .283f},
                     {.213f - c * .213f - s * .787f, .715f - c * .715f + s * .715f, .072f + c * .928f + s * .072f}};
    float Sm[3][3] = {{.213f + .787f * sat, .715f - .715f * sat, .072f - .072f * sat},
                      {.213f - .213f * sat, .715f + .285f * sat, .072f - .072f * sat},
                      {.213f - .213f * sat, .715f - .715f * sat, .072f + .928f * sat}};
    float M[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { M[i][j] = 0; for (int k = 0; k < 3; k++) M[i][j] += Sm[i][k] * H[k][j]; }
    for (size_t i = 0; i + 3 < gHeadMut.px.size(); i += 4) {
        float r = gHeadMut.px[i], g = gHeadMut.px[i + 1], b = gHeadMut.px[i + 2];
        gHeadMut.px[i] = (uint8_t)clampf(M[0][0] * r + M[0][1] * g + M[0][2] * b, 0, 255);
        gHeadMut.px[i + 1] = (uint8_t)clampf(M[1][0] * r + M[1][1] * g + M[1][2] * b, 0, 255);
        gHeadMut.px[i + 2] = (uint8_t)clampf(M[2][0] * r + M[2][1] * g + M[2][2] * b, 0, 255);
    }
}
const Image* headImage() { return mutantOn() ? &gHeadMut : gHead; }
const Image* shirtImage() { return gShirt; }
const Image* bottleImage() { return gBottle; }
Color skinColor() { return mutantOn() ? rgb(0x7ed957) : rgb(0xe9a97f); }

void emo(const std::string& s, float x, float y, float size) { gx::text(s, x, y, size, rgb(0xffffff), 1, 1); }
void txt(const std::string& s, float x, float y, float size, Color c, int weight) { gx::text(s, x, y, size, c, 1, 0, weight); }

// ============================================================ данные компании и напитков
struct Drink { const char* e; const char* n; int p; float d; bool skull; };
static const Drink DRINKS[6] = {
    {"🍺", "Пиво", 3, 12, false}, {"🍷", "Вино", 5, 16, false}, {"🥃", "Виски", 8, 28, false},
    {"🍸", "Коктейль", 7, 18, false}, {"🥂", "Шампанское", 9, 15, false}, {"", "Зелёный череп", 15, 45, true}};
struct Person { const char* id; const char* n; bool girl; uint32_t hat, beard, shirt; };
static const Person GUYS[2] = {{"sanya", "Саня", false, 0xe9e9e9, 0x6b4a2a, 0x6a8caf}, {"vitya", "Витя", false, 0x7d6652, 0x3b3b3b, 0x8a5a44}};
static const char* GIRL_ID[6] = {"alena", "katya", "masha", "dasha", "lena", "sonya"};
static const char* GIRL_NAME[6] = {"Алёна", "Катя", "Маша", "Даша", "Лена", "Соня"};
int girlCount = 6;
const char* girlId(int i) { return GIRL_ID[i % 6]; }
const char* girlName(int i) { return GIRL_NAME[i % 6]; }
const Image* girlPhoto(int i) { return asset_image("face" + std::to_string((i % 6) % 5)); }
int girlIndex(const std::string& id) { for (int i = 0; i < 6; i++) if (id == GIRL_ID[i]) return i; return -1; }
struct CrewState { float drunk = 0; bool away = false; double ask = 0; };
static std::map<std::string, CrewState> gCS;
static std::vector<std::string> crewIds() {
    std::vector<std::string> v; for (auto& g : GUYS) v.push_back(g.id);
    if (gCalled) for (auto& g : S.girls) v.push_back(g);
    return v;
}
static std::string personName(const std::string& id) {
    for (auto& g : GUYS) if (id == g.id) return g.n;
    int i = girlIndex(id); return i >= 0 ? GIRL_NAME[i] : id;
}

static void onAct(Act a, int arg);
static void onHold(Act a, bool down);
static void treat(const std::string& id);

// ============================================================ еда
struct FoodDef { const char* e; const char* n; int h, p; const char* s; int c; };
static const FoodDef FOODS[16] = {
    {"🍎", "Яблоко", 8, 0, "Хрум! Витамины!", 2}, {"🥒", "Огурчик", 6, 0, "Огурчик — огонь!", 2}, {"🍌", "Банан", 10, 2, "Банан — сила!", 2},
    {"🥟", "Пельмени", 22, 6, "Пельмешки с маслом, эх!", 2}, {"🍲", "Борщ", 28, 8, "Борщ со сметаной — имба!", 11}, {"🥞", "Блины", 20, 6, "Блинчики, ням!", 2},
    {"🌭", "Хот-дог", 18, 5, "Хот-дог! Классика!", 2}, {"🍔", "Бургер", 30, 10, "Бургер — это сила!", 2}, {"🍕", "Пицца", 30, 10, "Пицца — мой лучший друг!", 2},
    {"🍟", "Картошка фри", 14, 4, "Фри — самая вкусная!", 2}, {"🍗", "Курочка", 26, 9, "Ножка — просто бомба!", 2}, {"🥩", "Шашлык", 40, 15, "Шашлычок! Красота!", 2},
    {"🍣", "Суши", 24, 12, "Суши, как в ресторане!", 2}, {"🍩", "Пончик", 12, 4, "Сладенький!", 2}, {"🍰", "Тортик", 20, 9, "Тортик — высший пилотаж!", 2},
    {"🥤", "Квас", 6, 3, "Холодненький квасок!", 2}};

// ============================================================ UI: панель кнопок
static std::vector<Item> gPanel;
static float gMenuScroll = 0;
static int gPressed = -1;                  // индекс нажатой кнопки панели
static bool gMicRec = false;

static float btnFont(const Item& it) { return it.big ? 18.f : 13.5f; }
static void buildPanel() {
    gPanel.clear();
    auto btn = [&](const std::string& l, Act a, int arg = 0, bool big = false) { Item i; i.label = l; i.act = a; i.arg = arg; i.big = big; gPanel.push_back(i); return &gPanel.back(); };
    auto brk = [&]() { Item i; i.type = 3; gPanel.push_back(i); };
    auto hint = [&](const std::string& t) { Item i; i.type = 2; i.label = t; i.full = true; gPanel.push_back(i); };
    switch (gRoom) {
    case R_LIVING:
        { Item* m = btn(gMicRec ? "🔴 Слушаю…" : "🎤 Говорить", A_MIC); m->red = gMicRec; m->pulse = gMicRec; }
        btn("💃 Танцы", A_DANCE); btn("👋 Привет", A_SAYHI); break;
    case R_GAMES:
        btn("🎯 Тир", A_GAME, G_SHOOT); btn("🚬 Самокрутка", A_SMOKE); brk();
        btn("🎰 Аркада", A_GAME, G_ARCADE); btn("🃏 Дурак", A_GAME, G_DURAK); btn("💘 Знакомства", A_GAME, G_DATING); btn("🚔 Погоня", A_GAME, G_CHASE); brk();
        btn("✂️ Стрижка", A_GAME, G_CUT); btn("💞 Развлеки Надюху", A_GAME, G_NAD); break;
    case R_KITCHEN: {
        btn("🎮 Лови еду", A_GAME, G_CATCH); brk();
        Item m; m.type = 1; m.full = true;
        Card sk; sk.act = A_SKULL; sk.dis = S.coins < 25; sk.img = true; sk.n = "Зелёный череп"; sk.p = "25 🪙 · МУТАНТ ☣️"; m.cards.push_back(sk);
        for (int i = 0; i < 5; i++) if (S.fish[i] > 0) {
            Card c; c.act = A_FISHEAT; c.arg = i; c.e = FISH[i].e; c.n = std::string(FISH[i].n) + " ×" + std::to_string(S.fish[i]); c.p = "из улова · +" + std::to_string(FISH[i].h); m.cards.push_back(c);
        }
        for (int i = 0; i < 16; i++) {
            Card c; c.act = A_FOOD; c.arg = i; c.dis = S.coins < FOODS[i].p; c.e = FOODS[i].e; c.n = FOODS[i].n;
            c.p = (FOODS[i].p ? std::to_string(FOODS[i].p) + " 🪙" : std::string("бесплатно")) + " · +" + std::to_string(FOODS[i].h); m.cards.push_back(c);
        }
        gPanel.push_back(m); break; }
    case R_BEDROOM:
        if (gSleeping) btn("☀️ Разбудить", A_WAKE, 0, true);
        else { btn("😴 Уложить спать", A_SLEEP, 0, true); btn("💡 Свет", A_LIGHT); btn("🐑 Овцы", A_GAME, G_SHEEP); }
        break;
    case R_BAR: {
        Item m; m.type = 1; m.full = true;
        for (int i = 0; i < 6; i++) { Card c; c.act = A_DRINK; c.arg = i; c.dis = S.coins < DRINKS[i].p; c.sel = S.sel == i; c.img = DRINKS[i].skull; c.e = DRINKS[i].e; c.n = DRINKS[i].n; c.p = std::to_string(DRINKS[i].p) + " 🪙"; m.cards.push_back(c); }
        gPanel.push_back(m);
        brk();
        btn(std::string("📞 ") + (gCalled ? "Отпустить подруг" : "Позвать подруг (" + std::to_string(S.girls.size()) + ")"), A_CALL);
        btn("💘 Лови красоток", A_GAME, G_GIRLS); btn("🚬 Самокрутка", A_SMOKE); brk();
        hint("Тап по другу — налить ему выбранное"); break; }
    case R_BBQ:
        btn("🔥 Жарить шашлык", A_GAME, G_GRILL, true); btn("🍖 Съесть ×" + std::to_string(S.shashlik), A_EATSH); btn("🍺 Налить всем", A_TREATALL); btn("🚬 Самокрутка", A_SMOKE); break;
    case R_TOILET:
        btn("🚽 Сходить", A_GAME, G_TOILET, true); brk(); hint("Комфорт: " + std::to_string((int)std::lround(S.wc)) + "%"); break;
    case R_POND: fishingItems(gPanel); break;
    }
}

static void layoutPanel() {
    const float maxW = gx::W - 2 * 2 * VM, gap = 1.6f * VM;
    struct Row { std::vector<int> idx; float w = 0, h = 0; };
    std::vector<Row> rows; Row cur;
    auto flush = [&]() { if (!cur.idx.empty()) rows.push_back(cur); cur = Row(); };
    for (size_t i = 0; i < gPanel.size(); i++) {
        Item& it = gPanel[i];
        if (it.type == 3) { flush(); continue; }
        if (it.type == 0) {
            float fs = btnFont(it); float px = it.big ? 5 * VM : 2.4f * VM, py = it.big ? 2 * VM : 1.4f * VM;
            it.w = gx::textW(it.label, fs, 800) + 2 * px; it.h = fs * 1.3f + 2 * py;
        } else if (it.type == 1) { it.w = maxW; it.h = 76 + 2 * VM; }
        else { it.w = it.full ? maxW : gx::textW(it.label, 12, 700) + 4 * VM; it.h = 12 * 1.3f + 2 * .8f * VM; }
        if (it.type == 1 || (it.type == 2 && it.full)) { flush(); cur.idx.push_back((int)i); cur.w = it.w; cur.h = it.h; flush(); continue; }
        if (!cur.idx.empty() && cur.w + gap + it.w > maxW) flush();
        cur.w += (cur.idx.empty() ? 0 : gap) + it.w; cur.h = std::max(cur.h, it.h); cur.idx.push_back((int)i);
    }
    flush();
    float total = 0; for (size_t r = 0; r < rows.size(); r++) total += rows[r].h + (r ? gap : 0);
    float y = gx::H - NAVH - 6 - total;
    for (auto& r : rows) {
        float x = (gx::W - r.w) / 2;
        for (int i : r.idx) { Item& it = gPanel[i]; it.x = x; it.y = y + (r.h - it.h) / 2; x += it.w + gap; }
        y += r.h + gap;
    }
}

static void drawShadowText(const std::string& s, float x, float y, float size, Color c, int weight, int align = 1, int valign = 0) {
    gx::text(s, x, y + 1, size, rgb(0x000000, .55f), align, valign, weight);
    gx::text(s, x, y, size, c, align, valign, weight);
}

static void drawCard(const Card& c, float x, float y, float w, float h) {
    float a = c.dis ? .45f : 1.f;
    gx::rrect(x, y + .6f * VM, w, h, 2 * VM, rgba8(0x000000, 0x4d * a));
    gx::rrect(x, y, w, h, 2 * VM, rgb(0xffffff, .95f * a));
    if (c.sel) gx::rrectStroke(x - 1, y - 1, w + 2, h + 2, 2 * VM + 1, .6f * VM, rgb(0xffd23a));
    if (c.img) { const Image* b = bottleImage(); if (b) { float ih = 40.5f, iw = ih * b->w / b->h; gx::image(b, x + (w - iw) / 2, y + 4.5f, iw, ih, a); } }
    else gx::text(c.e, x + w / 2, y + 4.5f + 20, 36, rgb(0x222222, a), 1, 1);
    gx::text(c.n, x + w / 2, y + 4.5f + 44 + 6, 10.8f, rgb(0x222222, a), 1, 1, 800);
    gx::text(c.p, x + w / 2, y + 4.5f + 44 + 20, 10.35f, rgb(0x555555, a), 1, 1, 700);
}

static void drawPanel(float t) {
    for (size_t i = 0; i < gPanel.size(); i++) {
        Item& it = gPanel[i];
        if (it.type == 0) {
            float fs = btnFont(it), press = (gPressed == (int)i) ? .4f * VM : 0.f, sc = 1.f;
            if (it.pulse) sc = 1.f + .08f * (.5f - .5f * std::cos(t * 2 * PI_ / .5f));
            gx::save(); gx::translate(it.x + it.w / 2, it.y + it.h / 2 + press); gx::scale(sc, sc);
            float a = it.dis ? .5f : 1.f;
            if (!press) gx::rrect(-it.w / 2, -it.h / 2 + .5f * VM, it.w, it.h, 2.4f * VM, rgba8(0x000000, 0x33 * a));
            Color bg = it.red ? rgb(0xff5a5a, a) : it.big ? rgb(0xffd23a, a) : rgb(0xffffff, a);
            gx::rrect(-it.w / 2, -it.h / 2, it.w, it.h, 2.4f * VM, bg);
            gx::text(it.label, 0, 0, fs, it.red ? rgb(0xffffff, a) : rgb(0x222222, a), 1, 1, 800);
            gx::restore();
        } else if (it.type == 2) {
            gx::rrect(it.x, it.y, it.w, it.h, 2 * VM, rgba8(0x000000, 0xaa));
            gx::text(it.label, it.x + it.w / 2, it.y + it.h / 2, 12, rgb(0xffffff), 1, 1, 700);
        } else if (it.type == 1) {
            gx::save(); gx::clipRect(it.x, it.y - 2, it.w, it.h + 6);
            float cx = it.x - gMenuScroll, cy = it.y + VM, cw = std::max(19 * VM, 76.f), ch = 76, g = 1.4f * VM;
            for (auto& c : it.cards) { if (cx + cw > it.x - 4 && cx < it.x + it.w + 4) drawCard(c, cx, cy, cw, ch); cx += cw + g; }
            gx::restore();
        }
    }
}
static void clampMenuScroll() {
    for (auto& it : gPanel) if (it.type == 1) {
        float total = (float)it.cards.size() * (std::max(19 * VM, 76.f) + 1.4f * VM) - 1.4f * VM;
        gMenuScroll = clampf(gMenuScroll, 0, std::max(0.f, total - it.w));
    }
}

// ============================================================ HUD и навигация
static float dispH = 70, dispE = 85, dispW = 80;
static void drawHud(float dt) {
    float k = std::min(1.f, dt / .4f); dispH += (S.hunger - dispH) * k; dispE += (S.energy - dispE) * k; dispW += (S.wc - dispW) * k;
    std::string ct = "🪙 " + std::to_string(S.coins);
    if (mutantOn()) ct += " ☣️" + std::to_string((int)std::ceil((S.mutant - os::nowMs()) / 1000.0)) + "с";
    if (gDrunk > 8) ct += " 🍺" + std::to_string((int)std::lround(gDrunk)) + "%";
    float fs = 12.6f, cw = gx::textW(ct, fs, 800) + 4 * VM, gp = 2 * VM;
    float pad = 2 * VM, avail = gx::W - 2 * pad - cw - 3 * gp, sw = avail / 3, top = 1.6f * VM;
    gx::rrect(gx::W - pad - cw, top, cw, 24, 3 * VM, rgba8(0x000000, 0x88));
    drawShadowText(ct, gx::W - pad - cw + cw / 2, top + 12, fs, rgb(0xffffff), 800, 1, 1);
    const char* ic[3] = {"🍖", "⚡", "🚽"}; float val[3] = {dispH, dispE, dispW};
    Color c0[3] = {rgb(0xff9f43), rgb(0x4aa3ff), rgb(0x46c46a)}, c1[3] = {rgb(0xffd23a), rgb(0x7be0ff), rgb(0x9be86a)};
    for (int i = 0; i < 3; i++) {
        float x = pad + i * (sw + gp), mx = x + 22, mw = sw - 22, my = top + 12 - 5.4f, mh = 10.8f;
        drawShadowText(ic[i], x + 8, top + 12, fs, rgb(0xffffff), 800, 1, 1);
        gx::rrect(mx, my, mw, mh, 9, rgba8(0x000000, 0x88));
        float fw = mw * clampf(val[i], 0, 100) / 100.f;
        if (fw > 1) { Color cc[2] = {c0[i], c1[i]}; float of[2] = {0, 1}; gx::save(); gx::clipRect(mx, my, fw, mh); gx::grad(mx, my, fw, mh, false, cc, of, 2); gx::restore(); }
        gx::rrectStroke(mx + .5f, my + .5f, mw - 1, mh - 1, 9, 1, rgba8(0xffffff, 0x44));
    }
}
static const char* NAV_IC[8] = {"🏠", "🎮", "🍳", "🛏️", "🚽", "🎣", "🍖", "🍻"};
static const char* NAV_TX[8] = {"Гостиная", "Игры", "Кухня", "Спальня", "Туалет", "Рыбалка", "Шашлык", "Бар"};
static void drawNav() {
    float y = gx::H - NAVH; gx::rect(0, y, gx::W, NAVH, rgba8(0x10131c, 0xcc));
    float bw = gx::W / 8;
    for (int i = 0; i < 8; i++) {
        float x = i * bw; bool on = i == gRoom;
        if (on) { gx::rect(x, y, bw, NAVH, rgba8(0xffffff, 0x1f)); gx::rect(x, y, bw, 3, rgb(0xffd23a)); }
        gx::text(NAV_IC[i], x + bw / 2, y + 25, 22.5f, rgb(0xffffff), 1, 1, 400);
        gx::text(NAV_TX[i], x + bw / 2, y + 47, 11.7f, on ? rgb(0xffffff) : rgb(0xcfd6e6), 1, 1, 700);
    }
}

// ============================================================ комнаты
static void vg(float x, float y, float w, float h, uint32_t a, uint32_t b) {
    Color c[2] = {rgb(a), rgb(b)}; float o[2] = {0, 1}; gx::grad(x, y, w, h, true, c, o, 2);
}
static void splitBg(uint32_t t0, uint32_t t1, uint32_t bottom) {
    float y = gx::H * .62f; vg(0, 0, gx::W, y, t0, t1); gx::rect(0, y, gx::W, gx::H - y, rgb(bottom));
}
static std::vector<std::string> clusters(const std::string& s) {      // разбивка строки эмодзи
    std::vector<std::string> out; size_t i = 0;
    while (i < s.size()) {
        size_t n = 1; unsigned char c = s[i];
        if (c >= 0xF0) n = 4; else if (c >= 0xE0) n = 3; else if (c >= 0xC0) n = 2;
        std::string cl = s.substr(i, n); i += n;
        while (i + 2 < s.size() + 0 && (unsigned char)s[i] == 0xEF && (unsigned char)s[i + 1] == 0xB8 && (unsigned char)s[i + 2] == 0x8F) { cl += s.substr(i, 3); i += 3; }
        out.push_back(cl);
    }
    return out;
}
// ряд эмодзи с letter-spacing; x — левый край, ytop — верх строки
static float emojiRow(const std::string& s, float x, float ytop, float size, float spacing, bool rightAlign = false) {
    auto cl = clusters(s); float adv = size * 1.22f + spacing, total = adv * cl.size() - spacing;
    if (rightAlign) x -= total;
    for (size_t i = 0; i < cl.size(); i++) gx::text(cl[i], x + i * adv + size * .61f, ytop + size * .62f, size, rgb(0xffffff), 1, 1);
    return total;
}
static void box(float x, float y, float w, float h, float r, Color c) { gx::rrect(x, y, w, h, r, c); }

static void drawRoomBg() {
    const float W = gx::W, H = gx::H, topH = H * .62f;
    switch (gRoom) {
    case R_LIVING: case R_GAMES: {
        splitBg(0x0c2a14, 0x1f5a2c, 0x3b2a1a);
        const Image* b = bottleImage();
        if (b) { float tw = 70, th = 70.f * b->h / b->w; gx::save(); gx::clipRect(0, 0, W, topH);
            for (float y = 0; y < topH; y += th) for (float x = 0; x < W; x += tw) gx::image(b, x, y, tw, th, .22f);
            gx::restore(); }
        break; }
    case R_KITCHEN: {
        splitBg(0xf6ead0, 0xf0dcb4, 0x9a6b3c);
        float per = 11.9f * VM, ln = .4f * VM; Color c = rgb(0xc9b283, .5f);
        for (float x = 11.5f * VM; x < W; x += per) gx::rect(x, 0, ln, topH, c);
        for (float y = topH - 11.5f * VM - ln; y > -per; y -= per) gx::rect(0, y, W, ln, c);
        break; }
    case R_BEDROOM: {
        splitBg(0x3b3170, 0x4b3f8a, 0x5a3b22);
        for (float y = 0; y < topH; y += 90) for (float x = 0; x < W; x += 90) gx::ellipse(x + 18, y + 18, 1.5f, 1.5f, rgb(0xffffff, .5f));
        for (float y = 0; y < topH; y += 130) for (float x = 0; x < W; x += 130) gx::ellipse(x + 91, y + 52, 1.5f, 1.5f, rgb(0xffffff, .5f));
        break; }
    case R_POND: gx::rect(0, 0, W, H, rgb(0x9bd8ff)); break;
    case R_BAR: {
        splitBg(0x2b1810, 0x4a2a18, 0x1d120c);
        for (float x = 9 * VM; x < W; x += 9.3f * VM) gx::rect(x, 0, .3f * VM, topH, rgba8(0x000000, 0x33 * .8f));
        break; }
    case R_BBQ: {
        float y1 = H * .3f;
        vg(0, 0, W, y1, 0x8fd3f4, 0xd9f2ff); vg(0, y1, W, topH - y1, 0x3f8f4a, 0x2c6a38); gx::rect(0, topH, W, H - topH, rgb(0x4b6b2c));
        break; }
    case R_TOILET: {
        splitBg(0xcfe8f5, 0xe9f6fb, 0x7f9bb0);
        float per = 9.4f * VM, ln = .4f * VM; Color c = rgb(0xa9c6d6, .7f);
        for (float x = 9 * VM; x < W; x += per) gx::rect(x, 0, ln, topH, c);
        for (float y = topH - 9 * VM - ln; y > -per; y -= per) gx::rect(0, y, W, ln, c);
        break; }
    }
}

static void drawRoomDecor() {
    const float W = gx::W, H = gx::H;
    auto frac = [&](float p) { return H * p; };
    switch (gRoom) {
    case R_KITCHEN: {
        // окно
        float ww = 30 * VM, wh = 22 * VM, wx = W / 2 - ww / 2, wy = frac(.07f);
        box(wx - .5f * VM, wy - .5f * VM, ww + VM, wh + VM, 2.3f * VM, rgb(0xc9b283));
        box(wx, wy, ww, wh, 2 * VM, rgb(0xffffff));
        { Color c[2] = {rgb(0xbfe9ff), rgb(0xe8f8ff)}; float o[2] = {0, 1}; gx::save(); gx::clipRect(wx + 1.2f * VM, wy + 1.2f * VM, ww - 2.4f * VM, wh - 2.4f * VM); gx::grad(wx + 1.2f * VM, wy + 1.2f * VM, ww - 2.4f * VM, wh - 2.4f * VM, true, c, o, 2); gx::restore(); }
        // холодильник
        float fx = W * .03f, fw = 19 * VM, fh = 44 * VM, fb = H - frac(.36f), fy = fb - fh;
        box(fx, fy + VM, fw, fh, 2 * VM, rgba8(0x000000, 0x33));
        box(fx, fy, fw, fh, 2 * VM, rgb(0xc6d2da)); box(fx + .5f * VM, fy + .5f * VM, fw - VM, fh - VM, 1.6f * VM, rgb(0xe9eff3));
        gx::rect(fx, fy + fh * .34f, fw, .5f * VM, rgb(0xb8c6cf));
        box(fx + fw - 2 * VM - VM, fy + fh * .12f, VM, 8 * VM, VM, rgb(0x9fb0bb)); box(fx + fw - 3 * VM, fy + fh * .12f + 14 * VM, VM, 8 * VM, VM, rgb(0x9fb0bb));
        // столешница
        float cw = 34 * VM, ch = 17 * VM, cx = W - cw, cy = fb - ch;
        box(cx, cy + VM, cw, ch, VM, rgba8(0x000000, 0x33));
        gx::rect(cx, cy + 2 * VM, cw, ch - 2 * VM, rgb(0xa8713e)); box(cx, cy, cw, 2 * VM, VM, rgb(0xe4d6c0)); gx::rect(cx + VM, cy, cw - VM, 2 * VM, rgb(0xe4d6c0));
        const char* cs[3] = {"🍳", "🥘", "🍞"}; float cl[3] = {3, 15, 26};
        for (int i = 0; i < 3; i++) gx::text(cs[i], cx + cl[i] * VM + 18, cy + 2 * VM - 9 * VM + 22, 36, rgb(0xffffff), 1, 1);
        // полка
        float shRight = W - W * .06f - VM; float tw = emojiRow("🧂🫙🍯", shRight, frac(.12f), 7 * VM, VM, true);
        gx::rect(shRight - tw - VM, frac(.12f) + 7 * VM * 1.25f, tw + 2 * VM, 1.4f * VM, rgb(0x8b5a2b));
        break; }
    case R_BEDROOM: {
        float bx = W * .03f, bw = 50 * VM, bh = 20 * VM, bb = H - frac(.34f), by = bb - bh;
        box(bx, by + 6 * VM, bw, bh - 6 * VM, 2 * VM, rgb(0x7a4b26));
        box(bx + 2 * VM, by + 2 * VM, bw - 4 * VM, 12 * VM, 3 * VM, rgb(0xf3f1ff));
        box(bx + 4 * VM, by - VM + 1, 14 * VM, 8 * VM, 3 * VM, rgba8(0x000000, 0x33)); box(bx + 4 * VM, by - VM, 14 * VM, 8 * VM, 3 * VM, rgb(0xffffff));
        box(bx + 16 * VM, by + 3 * VM, bw - 18 * VM, 12 * VM, 2 * VM, rgb(0xc0392b, .95f));
        // лампа
        float lw = 9 * VM, lh = 26 * VM, lx = W - W * .06f - lw, ly = H - frac(.36f) - lh;
        Pt p[8] = {{lx + lw * .35f, ly}, {lx + lw * .65f, ly}, {lx + lw, ly + lh * .4f}, {lx + lw * .56f, ly + lh * .4f}, {lx + lw * .56f, ly + lh}, {lx + lw * .44f, ly + lh}, {lx + lw * .44f, ly + lh * .4f}, {lx, ly + lh * .4f}};
        gx::poly(p, 8, rgb(0xd9b878));
        gx::text("🌙", W - W * .10f - 27, frac(.08f) + 33, 12 * VM, rgb(0xffffff), 1, 1);
        break; }
    case R_BAR: {
        float t = (float)gT;
        float nx = W - W * .04f, ny = frac(.27f) + 9 * VM * .65f; float fs = 9 * VM;
        for (int i = 0; i < 12; i++) { float a = i * PI_ / 6; gx::text("БАР", nx - 3.5f * VM + std::cos(a) * 3.2f * VM, ny + std::sin(a) * 3.2f * VM, fs, rgb(0xff2f8a, .1f), 1, 1, 900); }
        gx::text("БАР", nx - 3.5f * VM, ny, fs, rgb(0xff5fa8, .92f + .08f * std::sin(t * 6)), 1, 1, 900);
        float sx = W * .04f + 2 * VM; float tw = emojiRow("🍾🥃🍷🍸", sx, frac(.24f), 8 * VM, 1.2f * VM);
        gx::rect(sx - 2 * VM, frac(.24f) + 8 * VM * 1.25f, tw + 4 * VM, 1.6f * VM, rgb(0x7a4b26));
        float cw = 38 * VM, ch = 20 * VM, cx = W - cw, cy = H - frac(.36f) - ch;
        gx::rect(cx, cy + 2.4f * VM, cw, ch - 2.4f * VM, rgb(0x6b3f1d)); box(cx, cy, cw, 2.4f * VM + 4, VM, rgb(0xa8713e)); gx::rect(cx + VM, cy, cw - VM, 2.4f * VM, rgb(0xa8713e));
        const char* ds[3] = {"🍺", "🥂", "🍹"}; float dl[3] = {3, 15, 27};
        for (int i = 0; i < 3; i++) gx::text(ds[i], cx + dl[i] * VM + 18, cy + 2.4f * VM - 10 * VM + 22, 36, rgb(0xffffff), 1, 1);
        break; }
    case R_BBQ: {
        gx::save(); gx::clipRect(0, 0, W, H);
        emojiRow("🌲🌳🌲🌲🌳🌲🌲🌳", 0, frac(.12f), 14 * VM, 0);
        gx::restore();
        float mw = 34 * VM, mh = 12 * VM, mx = W - W * .05f - mw, my = H - frac(.33f) - mh;
        box(mx, my + VM, mw, mh, VM, rgba8(0x000000, 0x40)); box(mx, my, mw, mh, VM, rgb(0x2b2b2b));
        emojiRow("🍢🍢🍢", mx + 2 * VM, my - 3 * VM, 7 * VM, VM);
        float fl = .85f + .15f * std::sin((float)gT * 9);
        gx::save(); gx::setAlpha(fl); emojiRow("🔥🔥", mx + 6 * VM, my - 9 * VM, 9 * VM, 0); gx::restore();
        emojiRow("🪵🪵", W * .03f, H - frac(.33f) - 12 * VM * 1.2f, 12 * VM, 0);
        break; }
    case R_TOILET: {
        gx::text("🚽", W - W * .08f - 76, H - frac(.34f) - 76, 34 * VM, rgb(0xffffff), 1, 1);
        gx::text("🧻", W - W * .36f - 25, frac(.2f) + 28, 11 * VM, rgb(0xffffff), 1, 1);
        break; }
    default: break;
    }
}

// ============================================================ персонаж
struct StageGeo { float cx, top, w, h; };
static float gStageCx = 225;
static StageGeo stageGeo() {
    float w = std::min(.82f * gx::W, .36f * gx::H), bo = NAVH + 6, cx = gx::W * .5f;
    switch (gRoom) {
    case R_KITCHEN: w = std::min(.68f * gx::W, .29f * gx::H); bo = NAVH + 146; break;
    case R_BEDROOM: cx = gx::W * .7f; break;
    case R_TOILET: cx = gx::W * .32f; break;
    case R_BAR: w = std::min(.66f * gx::W, .28f * gx::H); bo = NAVH + 170; cx = gx::W * .36f; break;
    case R_BBQ: w = std::min(.66f * gx::W, .28f * gx::H); bo = NAVH + 80; cx = gx::W * .36f; break;
    default: break;
    }
    gStageCx += (cx - gStageCx) * .25f;
    float h = w * 5.f / 3.f;
    return {gStageCx, gx::H - bo - h, w, h};
}
static bool stageVisible() { return gRoom != R_POND && !gSleeping && !gBushy; }

static void ringText(const std::string& s, float x, float y, float size, Color fill, float sw) {
    for (int i = 0; i < 14; i++) { float a = i * 2 * PI_ / 14; gx::text(s, x + std::cos(a) * sw / 2, y + std::sin(a) * sw / 2, size, rgb(0x000000), 1, 0, 900, 1); }
    gx::text(s, x, y, size, fill, 1, 0, 900, 1);
}
struct FoodFx { std::string e; bool img; double t0; };
static std::vector<FoodFx> gFood;

static void animTransform(float& ty, float& rot, float& sx, float& sy, float t) {
    ty = 0; rot = 0; sx = 1; sy = 1;
    double el = gT - aStart;
    bool act = aCls && gT < aEnd;
    auto lerp = [](double a, double b, double k) { return a + (b - a) * k; };
    if (act && aCls == 1) {            // shake .5s
        float k = (float)std::min(1.0, el / .5);
        static const float kf[6][2] = {{0, 0}, {.2f, -5}, {.4f, 5}, {.6f, -3}, {.8f, 3}, {1, 0}};
        for (int i = 0; i < 5; i++) if (k >= kf[i][0] && k <= kf[i + 1][0]) { rot = (float)lerp(kf[i][1], kf[i + 1][1], (k - kf[i][0]) / (kf[i + 1][0] - kf[i][0])); break; }
    } else if (act && aCls == 2) {     // jump .6s
        float k = (float)std::min(1.0, el / .6); ty = (k < .4f ? -.12f * (k / .4f) : -.12f * (1 - (k - .4f) / .6f));
    } else if (act && aCls == 3) {     // laugh .25s бесконечно
        float k = (float)std::fmod(el, .25) / .25f; float v = k < .5f ? k * 2 : (1 - k) * 2; ty = -.015f * v; rot = 1.f * v;
    } else if (act && aCls == 4) {     // dance .5s: ключи на 25% и 75%
        float k = (float)std::fmod(el, .5) / .5f;
        rot = k < .25f ? -6 * (k / .25f) : k < .75f ? -6 + 12 * (k - .25f) / .5f : 6 * (1 - (k - .75f) / .25f);
        ty = k < .25f ? -.03f * (k / .25f) : k < .75f ? -.03f : -.03f * (1 - (k - .75f) / .25f);
    } else {                            // дыхание 3с
        float k = .5f - .5f * std::cos((float)gT * 2 * PI_ / 3.f); sx = 1 + .012f * k; sy = 1 - .01f * k;
    }
}

static void drawBubble(const Bub& b, float cx, float top, float fs) {
    if (b.s < .02f || b.t.empty()) return;
    float tw = gx::textW(b.t, fs, 700), w = tw + 2 * fs, h = fs * 2.45f;
    gx::save(); gx::translate(cx, top + h / 2); gx::scale(b.s, b.s);
    box(-w / 2, -h / 2 + .4f * fs, w, h, 1.2f * fs, rgba8(0x000000, 0x44));
    box(-w / 2, -h / 2, w, h, 1.2f * fs, rgb(0xffffff));
    gx::text(b.t, 0, 0, fs, rgb(0x222222), 1, 1, 700);
    gx::restore();
}

static void drawCharacter(const StageGeo& g) {
    float k = g.w / 300.f; float t = (float)gT;
    gx::save();
    gx::translate(g.cx, g.top + g.h);
    if (gDrunk >= 40) { float per = gDrunk >= 75 ? 1.5f : 2.6f; gx::rotate(-3 * std::cos(t * 2 * PI_ / per)); }
    gx::translate(-g.w / 2, -g.h);

    float ty, rot, sx, sy; animTransform(ty, rot, sx, sy, t);
    gx::save();
    gx::translate(g.w / 2, g.h * .95f); gx::translate(0, ty * g.h); gx::rotate(rot); gx::scale(sx, sy); gx::translate(-g.w / 2, -g.h * .95f);

    // --- тело (SVG 300×500)
    gx::save(); gx::scale(k, k);
    Color skin = skinColor();
    box(95, 400, 48, 75, 20, rgb(0x3d5a80)); box(157, 400, 48, 75, 20, rgb(0x3d5a80));
    gx::ellipse(108, 478, 40, 16, rgb(0x2b2b2b)); gx::ellipse(192, 478, 40, 16, rgb(0x2b2b2b));
    box(128, 190, 44, 30, 14, skin);
    gx::save(); gx::translate(45, 248); gx::rotate(5); gx::translate(-45, -248); box(30, 248, 30, 115, 15, skin); gx::restore();
    if (shirtImage()) gx::image(shirtImage(), 22, 190, 256, 236);
    ringText("РОМЧИК", 126, 318, 22, rgb(0xffffff), 5);
    ringText("КРАСАВЧИК", 126, 344, 19, rgb(0xffd23a), 5);
    gx::line(262, 262, 236, 352, 32, skin, true);
    if (bottleImage()) gx::image(bottleImage(), 206, 276, 52, 116);
    gx::ellipse(232, 350, 17, 17, skin);
    { Pt q[9]; for (int i = 0; i <= 8; i++) { float u = i / 8.f; float x = (1 - u) * (1 - u) * 219 + 2 * (1 - u) * u * 232 + u * u * 245, y = (1 - u) * (1 - u) * 340 + 2 * (1 - u) * u * 332 + u * u * 340; q[i] = {x, y}; }
      gx::polyStroke(q, 9, false, 3, rgb(0xc98a60)); }
    gx::restore();

    // --- голова с челюстью
    {
        float hw = g.w * .62f, hh = hw * 370.f / 340.f, hx = (g.w - hw) / 2, hy = g.h * .02f;
        bool mut = mutantOn();
        gx::save();
        if (mut) { gx::translate(hx + hw / 2, hy + hh * .9f); gx::scale(1.1f, 1.1f); gx::translate(-(hx + hw / 2), -(hy + hh * .9f));
            for (int i = 0; i < 4; i++) gx::ellipse(hx + hw / 2, hy + hh / 2, hw * (.5f + .03f * i), hh * (.5f + .03f * i), rgb(0x5dff5d, .07f)); }
        float v = gJaw;
        if (v > .04f) {
            float mh = v * .08f * hh, mx = hx + hw * .09f, mw = hw * .82f, my = hy + hh * .67f;
            gx::rect(mx, my, mw, mh / 2, rgb(0x3a0f12)); box(mx, my, mw, mh, std::min(mh / 2, mw * .45f), rgb(0x3a0f12));
        }
        const Image* hd = headImage();
        if (hd) {
            gx::save(); gx::clipRect(hx, hy, hw, hh * .69f); gx::image(hd, hx, hy, hw, hh); gx::restore();
            gx::save(); gx::translate(0, v * .07f * hh); gx::clipRect(hx, hy + hh * .67f, hw, hh * .33f); gx::image(hd, hx, hy, hw, hh); gx::restore();
        }
        gx::restore();
    }
    gx::restore();   // char

    // --- эффекты на сцене
    if (mutantOn()) { float kk = .5f - .5f * std::cos(t * 2 * PI_ / 1.4f); gx::save(); gx::translate(g.w * .98f - 18, g.h * .02f + 18 - 2 * VM * kk); gx::rotate(15 * kk); emo("☣️", 0, 0, 8 * VM); gx::restore(); }
    if (gSmoking) {
        float sx0 = g.w * .52f, sy0 = g.h * .23f;
        gx::save(); gx::translate(sx0 + 16, sy0 + 16); gx::rotate(-20); emo("🚬", 0, 0, 7 * VM); gx::restore();
        float ph = std::fmod(t, 1.4f) / 1.4f, op = ph < .4f ? ph / .4f : 1 - (ph - .4f) / .6f, sc = .6f + ph * 1.0f;
        gx::save(); gx::setAlpha(std::max(0.f, op)); gx::translate(sx0 + 3 * VM + 14 + 3 * VM * ph, sy0 - 4 * VM + 14 - 10 * VM * ph); gx::scale(sc, sc); emo("💨", 0, 0, 6 * VM); gx::restore();
    }
    for (auto& f : gFood) {
        float kk = (float)((gT - f.t0) / .72); kk = clampf(kk, 0, 1); float e = kk * kk;
        float y = g.h * (-.10f + .36f * e);
        if (f.img) { if (bottleImage()) gx::image(bottleImage(), g.w / 2 - 11 * VM * .224f * 2.2f, y, 11 * VM * 155.f / 346.f, 11 * VM); }
        else emo(f.e, g.w / 2, y + 20, 9 * VM);
    }
    drawBubble(bub[W_MAIN], g.w / 2, -.05f * g.h, 15.3f);
    gx::restore();
}

static void roundPoly(float x, float y, float w, float h, float r, std::vector<Pt>& out) {
    r = std::min(r, std::min(w, h) / 2);
    const float cx[4] = {x + w - r, x + w - r, x + r, x + r}, cy[4] = {y + r, y + h - r, y + h - r, y + r};
    for (int c = 0; c < 4; c++) for (int i = 0; i <= 6; i++) { float a = (c * 90 + i * 15) * PI_ / 180.f - PI_ / 2; out.push_back({cx[c] + std::cos(a) * r, cy[c] + std::sin(a) * r}); }
}
// ============================================================ спящий Ромчик
static void drawSleeping() {
    float bw = std::min(.94f * gx::W, .78f * gx::H), bh = std::min(.5f * gx::W, .4f * gx::H);
    float bx = gx::W / 2 - bw / 2, bb = gx::H - (NAVH + .12f * gx::H), by = bb - bh;
    box(bx, by + .18f * bh + 2 * VM, bw, bh * .82f, 17, rgba8(0x000000, 0x50));
    box(bx, by + .18f * bh, bw, bh * .82f, 17, rgb(0x7a4b26));
    box(bx + .03f * bw, by + .08f * bh, .94f * bw, .6f * bh, 16, rgb(0xf3f1ff));
    box(bx + .05f * bw, by + .02f * bh + 3, .34f * bw, .34f * bh, 34, rgba8(0x000000, 0x33));
    box(bx + .05f * bw, by + .02f * bh, .34f * bw, .34f * bh, 34, rgb(0xffffff));
    float sw = .24f * bw, sh = sw * 370.f / 340.f;
    if (headImage()) { gx::save(); gx::translate(bx + .11f * bw + sw / 2, by - .26f * bh + sh / 2); gx::rotate(-82); gx::translate(-.04f * sw, 0); gx::image(headImage(), -sw / 2, -sh / 2, sw, sh); gx::restore(); }
    float kx = bx + .3f * bw, kw = .66f * bw, ky = by + .2f * bh, kh = .62f * bh;
    { std::vector<Pt> pp; roundPoly(kx, ky, kw, kh, 22, pp); gx::save(); gx::clipPoly(pp.data(), (int)pp.size()); Color c[2] = {rgb(0xd44b3b), rgb(0xa8322a)}; float o[2] = {0, 1}; gx::grad(kx, ky, kw, kh, true, c, o, 2); gx::restore(); }
    box(kx + .08f * kw, ky + .3f * kh, .84f * kw, .06f * kh, 4, rgba8(0xffffff, 0x40));
    const char* zs[3] = {"Z", "z", "Z"}; float zl[3] = {.34f, .38f, .30f}, zd[3] = {0, .7f, 1.4f};
    for (int i = 0; i < 3; i++) {
        float ph = (float)gT - zd[i]; if (ph < 0) continue; ph = std::fmod(ph, 2.f) / 2.f;
        float op = ph < .3f ? ph / .3f : 1 - (ph - .3f) / .7f;
        gx::text(zs[i], bx + zl[i] * bw + 8 * VM * ph, by - .18f * bh + 14 + (-10 * VM * ph), 7 * VM, rgb(0xffffff, op), 1, 1, 900);
    }
    drawBubble(bub[W_SLEEP], gx::W / 2, .12f * gx::H, 15.3f);
}

// ============================================================ компания (бар/шашлык)
struct CrewRect { std::string id; float x, y, w, h; };
static std::vector<CrewRect> gCrewRects;

static void quadPts(std::vector<Pt>& v, Pt a, Pt c, Pt b) {
    for (int i = 1; i <= 8; i++) { float u = i / 8.f, m = 1 - u; v.push_back({m * m * a.x + 2 * m * u * c.x + u * u * b.x, m * m * a.y + 2 * m * u * c.y + u * u * b.y}); }
}
static void drawGuy(const Person& p, float x, float y, float w, float drunk) {
    float s = w / 100.f; gx::save(); gx::translate(x, y); gx::scale(s, s);
    Color skin = rgb(0xf2c6a0);
    { std::vector<Pt> v{{8, 110}}; quadPts(v, {8, 110}, {8, 80}, {50, 78}); quadPts(v, {50, 78}, {92, 80}, {92, 110}); gx::poly(v.data(), (int)v.size(), rgb(p.shirt)); }
    box(41, 66, 18, 16, 6, skin); gx::ellipse(50, 48, 30, 30, skin);
    { std::vector<Pt> v{{22, 56}}; quadPts(v, {22, 56}, {50, 100}, {78, 56}); quadPts(v, {78, 56}, {74, 82}, {50, 88}); quadPts(v, {50, 88}, {26, 82}, {22, 56}); gx::poly(v.data(), (int)v.size(), rgb(p.beard)); }
    { std::vector<Pt> v{{17, 46}}; quadPts(v, {17, 46}, {18, 8}, {50, 10}); quadPts(v, {50, 10}, {82, 8}, {83, 46}); quadPts(v, {83, 46}, {50, 34}, {17, 46}); gx::poly(v.data(), (int)v.size(), rgb(p.hat)); }
    box(15, 42, 70, 7, 3.5f, rgb(p.hat));
    gx::ellipse(38, 50, 3.6f, 3.6f, rgb(0x222222)); gx::ellipse(62, 50, 3.6f, 3.6f, rgb(0x222222));
    gx::ellipse(39.2f, 48.8f, 1.1f, 1.1f, rgb(0xffffff)); gx::ellipse(63.2f, 48.8f, 1.1f, 1.1f, rgb(0xffffff));
    if (drunk >= 40) { gx::ellipse(29, 60, 7, 4.5f, rgb(0xff5a5a, .85f)); gx::ellipse(71, 60, 7, 4.5f, rgb(0xff5a5a, .85f)); }
    { std::vector<Pt> v{{41, 63}}; quadPts(v, {41, 63}, {50, 71}, {59, 63}); gx::polyStroke(v.data(), (int)v.size(), false, 3, rgb(0x7a2a2a)); }
    gx::restore();
}
static void drawFaceCircle(const Image* im, float x, float y, float d) {
    gx::ellipse(x + d / 2, y + d / 2 + 2, d / 2, d / 2, rgba8(0x000000, 0x66));
    if (im) {
        std::vector<Pt> c; for (int i = 0; i < 32; i++) { float a = i * 2 * PI_ / 32; c.push_back({x + d / 2 + std::cos(a) * d / 2, y + d / 2 + std::sin(a) * d / 2}); }
        float sd = (float)std::min(im->w, im->h);
        gx::save(); gx::clipPoly(c.data(), (int)c.size()); gx::image(im, x, y, d, d, 1, (im->w - sd) / 2, (im->h - sd) / 2, sd, sd); gx::restore();
    }
    gx::ellipseStroke(x + d / 2, y + d / 2, d / 2 - 1, d / 2 - 1, 2, rgb(0xffffff));
}
static void drawCrew() {
    gCrewRects.clear();
    auto ids = crewIds();
    const float iw = 14 * VM, gapX = 1.4f * VM, gapY = .8f * VM, maxW = gx::W - 4 * VM;
    struct It { std::string id; float h; };
    std::vector<std::vector<It>> rows(1); float rw = 0;
    for (auto& id : ids) {
        CrewState& c = gCS[id]; bool girl = girlIndex(id) >= 0;
        float ah = c.away ? 7 * VM * 1.25f : girl ? iw : iw * 1.1f, h = ah + 10 + 13;
        if (!rows.back().empty() && rw + gapX + iw > maxW) { rows.emplace_back(); rw = 0; }
        rw += (rows.back().empty() ? 0 : gapX) + iw; rows.back().push_back({id, h});
    }
    float y = 46;
    for (auto& row : rows) {
        float w = row.size() * iw + (row.size() ? row.size() - 1 : 0) * gapX, x = (gx::W - w) / 2, rh = 0;
        for (auto& it : row) {
            CrewState& c = gCS[it.id]; bool girl = girlIndex(it.id) >= 0;
            float ah = c.away ? 7 * VM * 1.25f : girl ? iw : iw * 1.1f;
            gCrewRects.push_back({it.id, x, y, iw, it.h});
            if (c.away) emojiRow("🌿🌿", x + iw / 2 - 7 * VM * 1.22f, y, 7 * VM, 0);
            else {
                gx::save();
                if (c.drunk >= 75) { gx::translate(x + iw / 2, y + ah); gx::rotate(-6 * std::cos((float)gT * 2 * PI_ / 1.6f)); gx::translate(-(x + iw / 2), -(y + ah)); }
                if (girl) drawFaceCircle(girlPhoto(girlIndex(it.id)), x, y, iw);
                else for (auto& g : GUYS) if (it.id == g.id) drawGuy(g, x, y, iw, c.drunk);
                gx::restore();
            }
            gx::rrect(x + 4, y + ah + 2, iw - 8, 6, 4, rgba8(0x000000, 0x77));
            float fw = (iw - 8) * clampf(c.drunk, 0, 100) / 100.f;
            if (fw > 1) { gx::save(); gx::clipRect(x + 4, y + ah + 2, fw, 6); Color cc[2] = {rgb(0xffd23a), rgb(0xff5a5a)}; float of[2] = {0, 1}; gx::grad(x + 4, y + ah + 2, iw - 8, 6, false, cc, of, 2); gx::restore(); }
            drawShadowText(personName(it.id), x + iw / 2, y + ah + 10 + 6.5f, 10.35f, rgb(0xffffff), 800, 1, 1);
            Bub& b = crewBub[it.id];
            if (b.s > .02f && !b.t.empty()) {
                float fs = 10.35f, tw = gx::textW(b.t, fs, 800) + 2 * .6f * fs, bh = fs * 1.3f + .6f * fs;
                gx::save(); gx::translate(x + iw / 2, y); gx::scale(b.s, b.s); gx::translate(0, -bh / 2);
                box(-tw / 2, -bh / 2 + 3, tw, bh, 6.3f, rgba8(0x000000, 0x66)); box(-tw / 2, -bh / 2, tw, bh, 6.3f, rgb(0xffffff));
                gx::text(b.t, 0, 0, fs, rgb(0x222222), 1, 1, 800); gx::restore();
            }
            rh = std::max(rh, it.h); x += iw + gapX;
        }
        y += rh + gapY;
    }
}

// ============================================================ логика: еда, сон, комнаты
static bool feedFlow(const std::string& emoji, float h, const std::string& phrase, const std::string& clip = "2", bool img = false, std::function<void()> afterFn = nullptr) {
    if (S.hunger >= 97) { sfx::nope(); anim("shake", 400); say("Не лезет уже! 😅"); return false; }
    gFood.push_back({emoji, img, gT});
    after(.72f, [=]() {
        if (!gFood.empty()) gFood.erase(gFood.begin());
        S.hunger = clampf(S.hunger + h, 0, 100); S.wc = clampf(S.wc - 2, 0, 100);
        playClip(clip, "jump"); say(phrase, 2600); if (afterFn) afterFn(); saveGame();
    });
    return true;
}
static void buySkull() {
    if (S.coins < 25) { sfx::nope(); say("Не хватает монет 🪙"); return; }
    if (feedFlow("", 10, "ААА! Я МУТАНТ! ☣️", "12", true, []() { S.mutant = os::nowMs() + 120000; })) S.coins -= 25;
}
static void buyFood(int i) {
    const FoodDef& f = FOODS[i];
    if (S.coins < f.p) { sfx::nope(); say("Не хватает монет 🪙"); return; }
    if (feedFlow(f.e, (float)f.h, f.s, std::to_string(f.c))) S.coins -= f.p;
}
static void eatFish(int i) {
    if (S.fish[i] <= 0) return;
    if (FISH[i].h == 0) { sfx::nope(); say("Ботинок не едят! Продай его."); return; }
    if (feedFlow(FISH[i].e, (float)FISH[i].h, pickS({"Ухааа, рыбка!", "Рыбалка — это жизнь!", "Свежайшая, сам поймал!"}))) S.fish[i]--;
}

static int gSleepTimer = 0;
static void sleepLoop() {
    if (!gSleeping) return;
    playClip("13"); const au::Clip* c = au::clip("13"); float d = c ? c->dur() : 11.3f;
    gSleepTimer = after(d + .7f, sleepLoop);
}
static void wake(bool silent) {
    cancelTimer(gSleepTimer); au::stopChannel(0); gSleeping = false;
    if (!silent) say(S.energy > 70 ? "Выспался! Красота!" : "Ещё бы поспать…", 2200);
}
static void sleepNow() {
    if (S.energy >= 95) { sfx::nope(); say("Я не хочу спать, я бодрый!"); return; }
    gSleeping = true; playClip("9"); after(3.3f, []() { if (gSleeping) sleepLoop(); }); say("Хррр…", 2000, W_SLEEP);
}

static void setRoom(int r) {
    if (gSleeping) wake(true);
    fishingReset(); gRoom = r; gMenuScroll = 0;
    if (r != R_BEDROOM) gLightsOff = false;
    static const char* hello[8] = {"Привет! Потыкай меня 👆", "Во что сыграем? 🎮", "Ооо, пора поесть!", "", "", "Ну, где там моя рыба?", "Шашлычок, мангал, пацаны!", "Налей-ка мне!"};
    std::string h = hello[r];
    if (r == R_BEDROOM) h = S.energy < 40 ? "Спать хочу…" : "Уютно тут";
    if (r == R_TOILET) h = S.wc < 40 ? "Ой, наконец-то!" : "Можно и сходить";
    if (r != R_POND) after(.2f, [h]() { say(h, 2000); }); else say(h, 2500, W_POND);
}

// ------------------------------------------------------------ напитки, компания, самокрутка
static void applyDrink(const Drink& d) {
    S.hunger = clampf(S.hunger + 2, 0, 100); S.wc = clampf(S.wc - 6, 0, 100);
    if (d.skull) S.mutant = os::nowMs() + 120000;
}
static void bushesMe() {
    playClip("18"); gBushy = true; say("", 100);
    after(8, []() { gBushy = false; gDrunk = 50; say("Фух… полегчало", 2500); });
}
static void drinkMe(int i) {
    const Drink& d = DRINKS[i]; S.sel = i;
    if (S.coins < d.p) { sfx::nope(); say("Не хватает монет 🪙"); return; }
    if (gBushy) return;
    bool wasted = gDrunk >= 75; S.coins -= d.p; applyDrink(d);
    playClip(d.skull ? "12" : "14", "jump");
    say(d.skull ? "АААА! МУТАНТ! ☣️" : pickS({"Ух, хорошо пошло!", "За здоровье!", "Наливай ещё!", "Кайф!"}), 2200);
    gDrunk = clampf(gDrunk + d.d, 0, 100);
    if (wasted && frand() < .55f) after(1.8f, bushesMe);
    saveGame();
}
static void treat(const std::string& id) {
    CrewState& c = gCS[id]; const Drink& d = DRINKS[S.sel];
    if (c.away) { say("Он в кустах 🌿"); return; }
    if (S.coins < d.p) { sfx::nope(); say("Не хватает монет 🪙"); return; }
    S.coins -= d.p; bool wasted = c.drunk >= 75; c.drunk = clampf(c.drunk + d.d, 0, 100);
    playClip(d.skull ? "12" : "14"); csay(id, pickS({"За тебя!", "Ого!", "Ещё!", "Ух!"}), 1600);
    if (wasted && frand() < .55f) {
        c.away = true;
        after(.2f, [id]() { playClip("18"); csay(id, "Бе-е-е…", 2500); });
        after(8, [id]() { CrewState& cc = gCS[id]; cc.away = false; cc.drunk = 50; csay(id, "Фух…", 1800); });
    }
    saveGame();
}
static void treatAll() {
    const Drink& d = DRINKS[S.sel]; std::vector<std::string> list;
    for (auto& id : crewIds()) if (!gCS[id].away) list.push_back(id);
    int cost = d.p * ((int)list.size() + 1);
    if (S.coins < cost) { sfx::nope(); say("Нужно " + std::to_string(cost) + " 🪙"); return; }
    S.coins -= cost;
    for (auto& id : list) { CrewState& c = gCS[id]; c.drunk = clampf(c.drunk + d.d, 0, 100); csay(id, "Ура!", 1500); }
    playClip("14"); gDrunk = clampf(gDrunk + d.d, 0, 100); applyDrink(d); say("За дружбу! 🍻", 2200); saveGame();
}
static void crewTick() {
    double now = os::nowMs();
    for (auto& kv : gCS) if (!kv.second.away) kv.second.drunk = clampf(kv.second.drunk - 2.5f, 0, 100);
    if (gDrunk >= 75 && now - gAsked > 22000 && !gBushy && !gSleeping) {
        gAsked = now; playClip("17"); say("ДОГОН! Налей ещё! 🍺", 3000); if (gRoom == R_POND) say("ДОГОН!", 2500, W_POND);
    }
    if (gRoom == R_BAR || gRoom == R_BBQ) for (auto& id : crewIds()) {
        CrewState& c = gCS[id];
        if (c.drunk >= 75 && !c.away && now - c.ask > 26000 && frand() < .4f) { c.ask = now; csay(id, "Догон!", 3000); if (now - gAsked > 5000) { gAsked = now; playClip("17"); } }
    }
}
static void smoke() {
    if (gSmoking || gSleeping) return;
    gSmoking = true; const au::Clip* c = au::clip("19"); float d = c ? c->dur() : 3.4f;
    playClip("19"); say("Дымок…", d * 1000);
    after(d + .25f, []() { gSmoking = false; playClip("20"); say("Фух, хорошо зашло…", 3200); S.energy = clampf(S.energy - 3, 0, 100); saveGame(); });
}

// ------------------------------------------------------------ голос (удерживать «Говорить»)
static int gMicTimer = 0;
static void stopRec() {
    if (!gMicRec) return;
    gMicRec = false; cancelTimer(gMicTimer);
    std::vector<int16_t> pcm = os::recStop();
    if (pcm.size() < 2000) return;
    float dur = pcm.size() / (float)au::RATE / 1.35f;
    au::playPCM(std::move(pcm), 0, 1.35f, 1.6f); say("🗣️", dur * 1000 + 300);
}
static void startRec() {
    if (gMicRec || au::playing(0)) return;
    if (!os::recStart()) { say("Нет доступа к микрофону", 2500); return; }
    gMicRec = true; gMicTimer = after(6, stopRec);
}

// ------------------------------------------------------------ обработка нажатий панели
static void onHold(Act a, bool down) {
    if (a == A_MIC) { if (down) startRec(); else stopRec(); }
    else if (a == A_HOLD) fishingAct(A_HOLD, down);
}
static void onAct(Act a, int arg) {
    switch (a) {
    case A_FOOD: buyFood(arg); break;
    case A_FISHEAT: eatFish(arg); break;
    case A_SKULL: buySkull(); break;
    case A_GAME:
        if (arg == G_TOILET) { if (S.wc > 85) { sfx::nope(); say("Пока не хочу…", 1800); break; } playClip("7"); }
        overlayOpen((GameId)arg); break;
    case A_DANCE: playClip("d", "dance"); say("Танцуем! 🕺", 3000); break;
    case A_SMOKE: smoke(); break;
    case A_DRINK: drinkMe(arg); break;
    case A_CALL:
        gCalled = !gCalled;
        if (gCalled && S.girls.empty()) { say("Подруг нет — лови красоток! 💘", 2500); gCalled = false; }
        else say(gCalled ? "Девчонки, идите сюда! 📞" : "Девчонки ушли", 2000);
        break;
    case A_EATSH:
        if (S.shashlik > 0) { if (feedFlow("🍖", 38, "Шашлык! Вот это сила!", "2")) S.shashlik--; }
        else { sfx::nope(); say("Шашлыка нет. Нажарь!"); }
        break;
    case A_TREATALL: treatAll(); break;
    case A_SAYHI: playClip("3", "jump"); say("Привет-привет!", 2000); break;
    case A_SLEEP: sleepNow(); break;
    case A_WAKE: wake(false); break;
    case A_LIGHT: gLightsOff = !gLightsOff; if (gLightsOff) playClip("8"); else sfx::pop(); break;
    case A_SELL: fishingSell(); break;
    case A_CAST: case A_PULL: case A_HOOK: fishingAct(a, true); break;
    default: break;
    }
}

// ============================================================ кадр и ввод
static float gLife = 0, gSaveT = 0, gDark = 0;
static void lifeTick() {
    if (gSleeping) {
        S.energy = clampf(S.energy + 5, 0, 100); S.hunger = clampf(S.hunger - .3f, 0, 100);
        if (S.energy >= 100) say("Выспался!", 2000, W_SLEEP);
    } else {
        S.hunger = clampf(S.hunger - .9f, 0, 100); S.energy = clampf(S.energy - .5f, 0, 100); S.wc = clampf(S.wc - .7f, 0, 100);
        Who w = gRoom == R_POND ? W_POND : W_MAIN;
        if (S.wc < 22 && frand() < .4f) say(pickS({"Мне в туалет!", "Ой, терпеть не могу…", "Где тут уборная?!"}), 2200, w);
        if (S.hunger < 20 && frand() < .4f) say(pickS({"Жрать хочу…", "Живот урчит 🍔", "Покорми меня!"}), 2200, w);
        else if (S.energy < 20 && frand() < .4f) say(pickS({"Глаза слипаются…", "Пора в кроватку 😴"}), 2200, w);
    }
    gDrunk = clampf(gDrunk - (gSleeping ? 6 : 2.5f), 0, 100);
    crewTick(); saveGame();
}

void game_init() {
    gHead = asset_image("head"); gShirt = asset_image("shirt"); gBottle = asset_image("bottle");
    makeMutantHead(); loadGame();
    gStageCx = gx::W / 2; setRoom(R_LIVING);
}
void game_shutdown() { stopRec(); doSave(); }

void game_frame(float dt) {
    dt = clampf(dt, 0, .1f); gT += dt; runTimers();
    auto upd = [&](Bub& b) { float tgt = gT < b.until ? 1.f : 0.f; b.s += clampf(tgt - b.s, -dt / .15f, dt / .15f); };
    for (auto& b : bub) upd(b);
    for (auto& kv : crewBub) upd(kv.second);
    float tgtJaw = au::playing(0) ? clampf(au::level(0) * 7, 0, 1) : 0.f; gJaw = tgtJaw;
    gLife += dt; while (gLife >= 4) { gLife -= 4; lifeTick(); }
    gSaveT += dt; if (gSaveT >= 1) { gSaveT = 0; if (gSaveDirty) { gSaveDirty = false; doSave(); } }
    fishingFrame(dt); overlayFrame(dt);
    float dk = (gSleeping || gLightsOff) ? 1.f : 0.f; gDark += clampf(dk - gDark, -dt / .6f, dt / .6f);

    // ---- рисование
    buildPanel(); layoutPanel(); clampMenuScroll();
    if (gRoom == R_POND) fishingDrawScene(); else { drawRoomBg(); drawRoomDecor(); }
    StageGeo g = stageGeo();
    if (stageVisible()) drawCharacter(g);
    if (gSleeping) drawSleeping();
    if (gBushy) { float tot = 3 * (90 * 1.22f), ytop = gx::H - (NAVH + 12) - 90 * 1.25f; gx::save(); gx::translate(gx::W * .36f, ytop + 56); gx::rotate(2 * std::sin((float)gT * 21)); gx::translate(-gx::W * .36f, -(ytop + 56)); emojiRow("🌿🌿🌿", gx::W * .36f - .3f * tot, ytop, 90, 0); gx::restore(); }
    if (gRoom == R_BAR || gRoom == R_BBQ) drawCrew(); else gCrewRects.clear();
    if (gRoom == R_POND) drawBubble(bub[W_POND], .26f * gx::W, .11f * gx::H, 15.3f);
    if (gRoom == R_POND) fishingDrawOverlay();
    if (gDark > .01f) gx::rect(0, 0, gx::W, gx::H, rgb(0x000000, .6f * gDark));
    if (gDrunk >= 75 && gRoom != R_POND) gx::rect(0, 0, gx::W, gx::H, rgb(0x9b5bff, .05f + .04f * std::sin((float)gT * 2)));
    drawHud(dt); drawPanel((float)gT); drawNav();
    overlayDraw();
}

static bool inRect(float x, float y, float rx, float ry, float rw, float rh) { return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh; }
static bool gMenuDown = false, gMenuMoved = false; static float gMenuX0 = 0, gMenuS0 = 0;
static int gHoldItem = -1;

void game_down(float x, float y) {
    if (overlayActive()) { overlayDown(x, y); return; }
    if (y >= gx::H - NAVH) { int i = (int)(x / (gx::W / 8)); if (i >= 0 && i < 8 && i != gRoom) setRoom(i); else if (i == gRoom) setRoom(i); return; }
    for (size_t i = 0; i < gPanel.size(); i++) {
        Item& it = gPanel[i];
        if (it.type == 0 && !it.dis && inRect(x, y, it.x, it.y, it.w, it.h)) {
            gPressed = (int)i; gHoldItem = (int)i;
            if (it.act == A_MIC || it.act == A_HOLD) onHold(it.act, true);
            return;
        }
        if (it.type == 1 && inRect(x, y, it.x, it.y, it.w, it.h + 6)) { gMenuDown = true; gMenuMoved = false; gMenuX0 = x; gMenuS0 = gMenuScroll; return; }
    }
    for (auto& r : gCrewRects) if (inRect(x, y, r.x, r.y, r.w, r.h)) { treat(r.id); return; }
    if (gSleeping) {
        float bw = std::min(.94f * gx::W, .78f * gx::H), bh = std::min(.5f * gx::W, .4f * gx::H), bx = gx::W / 2 - bw / 2, by = gx::H - (NAVH + .12f * gx::H) - bh;
        if (inRect(x, y, bx, by, bw, bh)) wake(false);
        return;
    }
    if (!stageVisible()) return;
    StageGeo g = stageGeo(); float lx = (x - (g.cx - g.w / 2)) / g.w * 100, ly = (y - g.top) / g.h * 100;
    auto hit = [&](float l, float t, float w, float h) { return lx >= l && lx <= l + w && ly >= t && ly <= t + h; };
    if (hit(74, 38, 20, 24) || hit(6, 38, 20, 24)) { playClip("3", "jump"); say("Привет-привет!", 2000); }
    else if (hit(20, 0, 60, 34)) { playClip("1", "shake"); say(pickS({"Ай!", "Эй, полегче!", "Не тыкай!", "Ну хватит 😅", "Ой!"}), 1800); }
    else if (hit(28, 44, 44, 22)) { playClip("4", "laugh"); say(pickS({"Ха-ха-ха!", "Щекотно!", "Хи-хи-хи!", "Ой, не надо!"}), 1500); }
    else if (hit(25, 84, 50, 16)) { au::tone(180, .4f, 0, .3f, 600); anim("jump", 600); say("Оп!", 800); }
}
void game_move(float x, float y) {
    if (overlayActive()) { overlayMove(x, y); return; }
    if (gMenuDown) { if (std::fabs(x - gMenuX0) > 6) gMenuMoved = true; if (gMenuMoved) { gMenuScroll = gMenuS0 + (gMenuX0 - x); clampMenuScroll(); } }
}
void game_up(float x, float y) {
    if (overlayActive()) { overlayUp(x, y); return; }
    if (gMenuDown) {
        gMenuDown = false;
        if (!gMenuMoved) for (auto& it : gPanel) if (it.type == 1) {
            float cx = it.x - gMenuScroll, cw = std::max(19 * VM, 76.f), g = 1.4f * VM;
            for (auto& c : it.cards) { if (x >= cx && x <= cx + cw && y >= it.y && y <= it.y + it.h && !c.dis) { onAct(c.act, c.arg); break; } cx += cw + g; }
        }
        return;
    }
    if (gHoldItem >= 0 && gHoldItem < (int)gPanel.size()) {
        Item it = gPanel[gHoldItem]; gHoldItem = -1; gPressed = -1;
        if (it.act == A_MIC || it.act == A_HOLD) { onHold(it.act, false); return; }
        if (inRect(x, y, it.x, it.y, it.w, it.h)) onAct(it.act, it.arg);
    } else gPressed = -1;
}
void game_wheel(float x, float y, float d) {
    (void)x; (void)y; if (overlayActive()) return;
    gMenuScroll += d; clampMenuScroll();
}
void game_key(int code, bool down) {
    if (overlayActive()) { overlayKey(code, down); return; }
    fishingKey(code, down);
}
