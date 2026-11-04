// Портативный микшер: звуки-клипы, синтезированные «пиканья», уровень громкости для липсинка.
#include "platform.h"
#include <atomic>
#include <cmath>
#include <algorithm>

namespace {
struct Voice {
    const int16_t* data = nullptr; std::vector<int16_t> own; int n = 0;
    double pos = 0; float speed = 1, gain = 1; int channel = -1; int id = 0; float rms = 0;
    float delay = 0;            // сек до старта (для тонов)
    bool alive = false;
};
std::vector<Voice> V;
std::atomic_flag lk = ATOMIC_FLAG_INIT;
int nextId = 1;
struct Lock { Lock() { while (lk.test_and_set(std::memory_order_acquire)) {} } ~Lock() { lk.clear(std::memory_order_release); } };
float chLevel[512];
}

namespace au {
static int addVoice(Voice v) {
    Lock l; v.alive = true; v.id = nextId++;
    if (v.channel >= 0) for (auto& o : V) if (o.alive && o.channel == v.channel) o.alive = false;
    for (auto& o : V) if (!o.alive) { o = std::move(v); return o.id; }
    V.push_back(std::move(v)); return V.back().id;
}
int play(const Clip* c, int channel, float speed, float gain) {
    if (!c || !c->pcm) return 0;
    Voice v; v.data = c->pcm; v.n = c->n; v.speed = speed * (c->rate / (float)RATE); v.gain = gain; v.channel = channel;
    if (channel >= 0 && channel < 512) chLevel[channel] = 0;
    return addVoice(std::move(v));
}
int playPCM(std::vector<int16_t> pcm, int channel, float speed, float gain) {
    Voice v; v.own = std::move(pcm); v.data = v.own.data(); v.n = (int)v.own.size(); v.speed = speed; v.gain = gain; v.channel = channel;
    if (v.n == 0) return 0;
    Voice* dummy = nullptr; (void)dummy;
    // vector::data() после move остаётся валидным, но Voice будет перемещён ещё раз — пересчитаем указатель при рендере
    v.data = nullptr;
    return addVoice(std::move(v));
}
void stopChannel(int ch) { Lock l; for (auto& o : V) if (o.alive && o.channel == ch) o.alive = false; if (ch >= 0 && ch < 512) chLevel[ch] = 0; }
bool playing(int ch) { Lock l; for (auto& o : V) if (o.alive && o.channel == ch) return true; return false; }
float level(int ch) { return (ch >= 0 && ch < 512) ? chLevel[ch] : 0.f; }

void tone(float freq, float dur, int type, float vol, float slideTo, float delay) {
    int n = (int)(dur * RATE); if (n < 1) return;
    Voice v; v.own.resize(n); v.n = n; v.speed = 1; v.gain = 1; v.channel = -1; v.delay = delay;
    double ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float f = slideTo > 0 ? freq * std::pow(slideTo / freq, t) : freq;
        ph += f / RATE; ph -= std::floor(ph);
        float s = type == 0 ? std::sin(ph * 6.2831853) : type == 1 ? (ph < .5 ? 1.f : -1.f) : type == 2 ? (float)(2 * ph - 1) : (float)(4 * std::fabs(ph - .5) - 1);
        float env = vol * std::pow(.001f / vol, t);          // экспоненциальный спад до .001
        v.own[i] = (int16_t)(s * env * 32767 * .6f);
    }
    v.data = nullptr;
    addVoice(std::move(v));
}

void render(int16_t* out, int n) {
    static std::vector<float> acc; acc.assign(n, 0.f);
    Lock l;
    float levels[512]; bool touched[512] = {};
    for (auto& v : V) {
        if (!v.alive) continue;
        if (!v.data && !v.own.empty()) v.data = v.own.data();
        double sq = 0; int cnt = 0;
        for (int i = 0; i < n; i++) {
            if (v.delay > 0) { v.delay -= 1.f / RATE; continue; }
            int p = (int)v.pos;
            if (p >= v.n - 1) { v.alive = false; break; }
            float fr = (float)(v.pos - p);
            float s = (v.data[p] * (1 - fr) + v.data[p + 1] * fr) / 32768.f;
            acc[i] += s * v.gain; sq += s * s; cnt++;
            v.pos += v.speed;
        }
        if (v.channel >= 0 && v.channel < 512) {
            float r = cnt ? std::sqrt((float)(sq / cnt)) : 0.f;
            levels[v.channel] = touched[v.channel] ? std::max(levels[v.channel], r) : r; touched[v.channel] = true;
        }
    }
    for (int c = 0; c < 512; c++) chLevel[c] = touched[c] ? levels[c] : 0.f;
    for (int i = 0; i < n; i++) { float s = std::max(-1.f, std::min(1.f, acc[i])); out[i] = (int16_t)(s * 32767); }
}
}
