// effects.h — движок эффектов.
//
// Эффект — чистая функция, которая заполняет буфер сегмента для момента
// времени t. Никакого внутреннего состояния между кадрами: так эффекты
// тривиально тестируются, а перезагрузка не рвёт анимацию посередине.
//
// Порядок в реестре задаёт индекс fx, который уходит в Home Assistant.
// Менять порядок после релиза нельзя — сломаются сохранённые автоматизации.
#pragma once

#include <Arduino.h>

#include "core/state.h"

namespace lumen {

struct EffectContext {
    Rgb*     buffer = nullptr;  // буфер сегмента
    uint16_t length = 0;
    uint32_t timeMs = 0;        // время с запуска
    uint8_t  speed = 128;
    uint8_t  intensity = 128;
    uint8_t  palette = 0;
    Rgb      primary{255, 255, 255};
    Rgb      secondary{0, 0, 0};
};

using EffectFn = void (*)(EffectContext&);

uint8_t effectCount();
const char* effectName(uint8_t index);
void renderEffect(uint8_t index, EffectContext& ctx);

uint8_t paletteCount();
const char* paletteName(uint8_t index);

// Цвет из палитры по позиции 0..255. Палитра 0 («Default») возвращает
// primary — так «сплошной цвет» работает без спецслучаев в каждом эффекте.
Rgb paletteColor(uint8_t palette, uint8_t pos, const Rgb& primary);

}  // namespace lumen
