// live_state.h — предпросмотр кадра и подсветка диода для редактора раскладки.
//
// Рендер (свой таск или основной цикл) кладёт сюда последний кадр, веб-сервер
// забирает его и шлёт подписчикам по WebSocket. Подсветка диода идёт в обратную
// сторону: веб-сервер ставит номер, рендер читает и зажигает только его.
#pragma once

#include <Arduino.h>

namespace lumen {

// Есть ли подписчики на кадры. Пока нет, рендер ничего не копирует.
void    liveSetWanted(bool wanted);
bool    liveWanted();

// Рендер: последний кадр (R, G, B по порядку провода, уже с яркостью).
void    livePublish(const uint8_t* rgb, uint16_t count);

// Веб: копирует кадр в dst (не больше cap диодов). 0 — нового кадра нет.
uint16_t liveRead(uint8_t* dst, uint16_t cap);

// Подсветка диода. led < 0 — выключить. Гаснет сама через kIdentifyMs, если
// страница пропала, не успев снять подсветку.
void    liveIdentify(int led, uint32_t nowMs);
int     liveIdentified(uint32_t nowMs);   // -1 — ничего не горит

}  // namespace lumen
