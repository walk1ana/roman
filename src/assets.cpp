#include "platform.h"
#include <cstring>
#include <map>
#include <memory>

namespace {
std::map<std::string, std::unique_ptr<Image>> g_img;
std::map<std::string, au::Clip> g_clip;
std::vector<uint8_t> g_blob;
}
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool assets_load(const uint8_t* d, size_t size) {
    if (size < 8 || memcmp(d, "RMCK", 4)) return false;
    g_blob.assign(d, d + size);                       // владеем копией — указатели PCM остаются валидными
    const uint8_t* b = g_blob.data();
    uint32_t cnt = rd32(b + 4);
    for (uint32_t i = 0; i < cnt; i++) {
        const uint8_t* e = b + 8 + i * 40;
        char name[25]; memcpy(name, e, 24); name[24] = 0;
        uint32_t typ = rd32(e + 24), a = rd32(e + 28), bb = rd32(e + 32), off = rd32(e + 36);
        if (typ == 1) {
            auto im = std::make_unique<Image>(); im->w = a; im->h = bb;
            im->px.assign(b + off, b + off + (size_t)a * bb * 4);
            g_img[name] = std::move(im);
        } else {
            au::Clip c; c.pcm = reinterpret_cast<const int16_t*>(b + off); c.n = a; c.rate = bb;
            g_clip[name] = c;
        }
    }
    return true;
}
const Image* asset_image(const std::string& n) { auto it = g_img.find(n); return it == g_img.end() ? nullptr : it->second.get(); }
namespace au { const Clip* clip(const std::string& n) { auto it = g_clip.find(n); return it == g_clip.end() ? nullptr : &it->second; } }
