// liveview.h — то, что рендер отдаёт редактору раскладки.
//
// Две вещи, обе без Arduino, чтобы проверять на хосте:
//   * упаковка кадра для бинарного сообщения WebSocket;
//   * подсветка одного диода: пользователь держит палец на диоде в редакторе,
//     а на лампе горит именно он.
#pragma once

#include <cstddef>
#include <cstdint>

namespace lumen {
namespace liveview {

// Бинарное сообщение: [kFrameType][число диодов: старший байт][младший]
// и дальше R, G, B каждого диода по порядку провода.
constexpr uint8_t kFrameType   = 1;
constexpr size_t  kHeaderBytes = 3;

inline size_t frameBytes(uint16_t count) { return kHeaderBytes + 3u * count; }

// out должен вмещать frameBytes(count) байт. Возвращает число записанных байт.
size_t packFrame(const uint8_t* rgb, uint16_t count, uint8_t* out);

// Гасит весь кадр и зажигает диод led цветом on. led < 0 — ничего не менять.
// led за пределами ленты — кадр остаётся чёрным: так видно, что такого диода
// на ленте нет.
template <typename Px>
void applyIdentify(Px* px, uint16_t count, int led, const Px& on) {
    if (led < 0) return;
    for (uint16_t i = 0; i < count; ++i) px[i] = Px();
    if (led < count) px[led] = on;
}

}  // namespace liveview
}  // namespace lumen
