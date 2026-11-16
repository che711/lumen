// Тесты кадра предпросмотра и подсветки диода. Собираются на хосте:
//
//   g++ -std=c++17 -Wall -Wextra -I lib/liveview lib/liveview/*.cpp
//       test/test_liveview/test_main.cpp -o /tmp/lumen_liveview && /tmp/lumen_liveview

#include <cstdio>
#include <vector>

#include "liveview.h"

using namespace lumen::liveview;

static int g_failed = 0;
static int g_total  = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        g_total++;                                                    \
        if (!(cond)) {                                                \
            g_failed++;                                               \
            std::printf("  FAIL  %s\n        (%s:%d)\n", msg,         \
                        __FILE__, __LINE__);                          \
        }                                                             \
    } while (0)

#define SECTION(name) std::printf("\n== %s\n", name)

struct Px {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Px& o) const { return r == o.r && g == o.g && b == o.b; }
};

static void testPack() {
    SECTION("упаковка кадра");
    const uint8_t rgb[6] = {255, 0, 0, 1, 2, 3};
    uint8_t out[16] = {};
    const size_t n = packFrame(rgb, 2, out);
    CHECK(n == 9 && n == frameBytes(2), "длина: заголовок 3 + 3 байта на диод");
    CHECK(out[0] == kFrameType, "тип кадра");
    CHECK(out[1] == 0 && out[2] == 2, "число диодов");
    CHECK(out[3] == 255 && out[4] == 0 && out[5] == 0 && out[6] == 1 && out[8] == 3,
          "цвета по порядку");

    // Больше 255 диодов: число лежит в двух байтах.
    std::vector<uint8_t> big(3 * 1200, 7), msg(frameBytes(1200));
    packFrame(big.data(), 1200, msg.data());
    CHECK(msg[1] == 0x04 && msg[2] == 0xB0, "1200 = 0x04B0");
    CHECK(msg.size() == 3 + 3600 && msg.back() == 7, "хвост кадра на месте");

    uint8_t empty[4] = {9, 9, 9, 9};
    CHECK(packFrame(nullptr, 0, empty) == 3 && empty[1] == 0 && empty[2] == 0,
          "пустой кадр — один заголовок");
}

static void testIdentify() {
    SECTION("подсветка диода");
    const Px white{255, 255, 255};
    std::vector<Px> f(5, Px{10, 20, 30});

    applyIdentify(f.data(), 5, -1, white);
    CHECK(f[0] == (Px{10, 20, 30}) && f[4] == (Px{10, 20, 30}), "led < 0 — кадр не тронут");

    applyIdentify(f.data(), 5, 2, white);
    bool only = true;
    for (int i = 0; i < 5; ++i) only = only && (f[i] == (i == 2 ? white : Px{}));
    CHECK(only, "горит только выбранный диод, остальные чёрные");

    applyIdentify(f.data(), 5, 4, white);
    CHECK(f[4] == white && f[2] == Px{}, "последний диод");

    applyIdentify(f.data(), 5, 5, white);
    bool dark = true;
    for (const Px& p : f) dark = dark && (p == Px{});
    CHECK(dark, "номер за пределами ленты — кадр чёрный");

    applyIdentify(f.data(), 0, 0, white);
    CHECK(true, "пустая лента не падает");
}

int main() {
    std::printf("Lumen — тесты предпросмотра");
    testPack();
    testIdentify();
    std::printf("\n----------------------------------------\n");
    std::printf("Пройдено %d из %d\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
