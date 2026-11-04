// Общие объявления модулей игры (game.cpp, fishing.cpp, overlay.cpp)
#pragma once
#include "platform.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

constexpr float VM = 4.5f;                  // 1vmin оригинала в виртуальных пикселях
constexpr float NAVH = 62.f;                // высота нижней панели
constexpr float PI_ = 3.14159265358979f;

extern double gT;                           // игровое время, сек
float frand(); float frand(float a, float b); int irand(int n);
inline float clampf(float v, float a, float b) { return std::max(a, std::min(b, v)); }
inline std::string pickS(std::initializer_list<const char*> l) { return *(l.begin() + irand((int)l.size())); }

struct FishDef { const char* id; const char* e; const char* n; int v, h, w; float sp, d; };
extern const FishDef FISH[5];               // 0 плотва, 1 окунь, 2 лещ, 3 щука, 4 ботинок
struct State {
    float hunger = 70, energy = 85, wc = 80; int coins = 25;
    int fish[5] = {0, 0, 0, 0, 0}; int caught = 0; int best = -1;
    double mutant = 0;                      // unix-мс окончания мутации
    std::vector<std::string> girls; int shashlik = 0; int sel = 0;
};
extern State S;
extern float gDrunk;                        // D.me — опьянение Ромчика

enum Who { W_MAIN, W_POND, W_SLEEP };
void say(const std::string& t, float ms = 1800, Who w = W_MAIN);
int after(float sec, std::function<void()> fn);
void cancelTimer(int id);
void anim(const char* cls, float ms);       // shake / jump / laugh / dance
void playClip(const std::string& name, const char* cls = nullptr);
void extraSound(int n);
namespace sfx { void splash(); void bite(); void reel(); void win(); void lose(); void coin(); void pop(); void nope(); }
bool mutantOn(); int addCoins(int n); void saveGame();
void bumpHud();
extern int gRoom;                           // см. enum Room
enum Room { R_LIVING, R_GAMES, R_KITCHEN, R_BEDROOM, R_TOILET, R_POND, R_BBQ, R_BAR };
extern bool gSleeping;
extern float gMutantSkinMix;

// рисование
void emo(const std::string& s, float x, float y, float size);                       // центр, middle
void txt(const std::string& s, float x, float y, float size, Color c, int weight = 800);   // центр, baseline
Color skinColor();
const Image* headImage();                   // с учётом мутации
const Image* shirtImage();
const Image* bottleImage();

// панель кнопок
enum Act {
    A_NONE, A_FOOD, A_FISHEAT, A_SKULL, A_GAME, A_DANCE, A_SMOKE, A_DRINK, A_CALL, A_EATSH, A_TREATALL, A_SAYHI,
    A_SLEEP, A_WAKE, A_LIGHT, A_SELL, A_MIC, A_CAST, A_PULL, A_HOOK, A_HOLD
};
struct Card { Act act = A_NONE; int arg = 0; bool dis = false, sel = false, img = false; std::string e, n, p; };
struct Item {
    int type = 0;                           // 0 кнопка, 1 меню карточек, 2 подсказка
    std::string label; Act act = A_NONE; int arg = 0;
    bool big = false, red = false, pulse = false, dis = false, full = false;
    std::vector<Card> cards;
    float x = 0, y = 0, w = 0, h = 0;
};

// игры (overlay.cpp)
enum GameId { G_SHOOT, G_CATCH, G_SHEEP, G_GIRLS, G_GRILL, G_TOILET, G_CUT, G_NAD, G_ARCADE, G_DURAK, G_DATING, G_CHASE };
void overlayOpen(GameId g);
bool overlayActive();
void overlayFrame(float dt);
void overlayDraw();
bool overlayDown(float x, float y);
void overlayMove(float x, float y);
void overlayUp(float x, float y);
void overlayKey(int code, bool down);
extern int girlCount;                       // 6
const char* girlId(int i); const char* girlName(int i); const Image* girlPhoto(int i);
int girlIndex(const std::string& id);

// рыбалка (fishing.cpp)
void fishingReset();
void fishingFrame(float dt);
void fishingDrawScene();                    // сцена пруда (вместо комнаты)
void fishingDrawOverlay();                  // мини-игра «держи» и карточка улова
void fishingItems(std::vector<Item>& out);
void fishingAct(Act a, bool down);          // A_CAST/A_PULL/A_HOOK/A_HOLD/A_SELL
void fishingSell();
void fishingKey(int code, bool down);
int fishInvCount(int* worth);
void drawFisher(bool inPond);               // «рыбак»-фигурка, использует skin
