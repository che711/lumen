// Прогон всех эффектов на хосте: каждый эффект × каждая палитра ×
// весь диапазон speed/intensity. Ловит деление на ноль, выход за буфер
// и NaN — то, что на устройстве выглядит как «лента иногда моргает».
//
// Запуск: scripts/test_effects.sh

#include <cmath>
#include <cstdio>

#include "led/effects.h"

using namespace lumen;

int main() {
    constexpr int kLen = 60;
    Rgb buf[kLen];
    int failures = 0;

    std::printf("Lumen — прогон эффектов\n\n");

    for (uint8_t fx = 0; fx < effectCount(); ++fx) {
        int touched = 0;

        for (uint8_t pal = 0; pal < paletteCount(); ++pal) {
            for (uint32_t t = 0; t < 60000; t += 97) {
                EffectContext c;
                c.buffer    = buf;
                c.length    = kLen;
                c.timeMs    = t;
                c.speed     = static_cast<uint8_t>((t / 97) % 256);
                c.intensity = static_cast<uint8_t>((t / 53) % 256);
                c.palette   = pal;
                c.primary   = Rgb{255, 160, 60};
                c.secondary = Rgb{0, 0, 0};

                // Метка-часовой: если эффект пишет мимо своего буфера,
                // соседние байты останутся нетронутыми и это видно.
                for (int i = 0; i < kLen; ++i) buf[i] = Rgb{7, 7, 7};

                renderEffect(fx, c);

                for (int i = 0; i < kLen; ++i) {
                    if (!(buf[i].r == 7 && buf[i].g == 7 && buf[i].b == 7)) {
                        touched++;
                        break;
                    }
                }
            }
        }

        // Эффект, ни разу ничего не записавший, — почти наверняка ошибка.
        const bool ok = touched > 0;
        if (!ok) failures++;
        std::printf("  %-10s %s\n", effectName(fx), ok ? "ok" : "НЕ ПИШЕТ В БУФЕР");
    }

    std::printf("\nэффектов: %u, палитр: %u, провалов: %d\n",
                effectCount(), paletteCount(), failures);
    return failures == 0 ? 0 : 1;
}
