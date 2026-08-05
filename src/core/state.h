// state.h — единственный источник правды о том, что сейчас на ленте.
//
// Структура намеренно повторяет модель WLED (сегменты, fx/sx/ix/pal),
// потому что именно её ожидает нативная интеграция Home Assistant.
#pragma once

#include <Arduino.h>

#include <vector>

#include "config.h"

namespace lumen {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

struct Segment {
    bool    active = false;
    uint16_t start = 0;
    uint16_t stop  = 0;   // не включительно, как в WLED
    bool    on     = true;
    uint8_t bri    = 255;

    uint8_t fx      = 0;  // индекс эффекта
    uint8_t speed   = 128;
    uint8_t intensity = 128;
    uint8_t palette = 0;

    Rgb primary{255, 160, 60};
    Rgb secondary{0, 0, 0};

    bool reverse = false;

    uint16_t length() const { return stop > start ? stop - start : 0; }
};

// Глобальное состояние. Мутируется из веб-обработчиков и планировщика,
// читается рендером — поэтому все изменения идут через мьютекс.
struct AppState {
    bool     on         = true;
    uint8_t  brightness = 128;   // «пользовательская» яркость 0..255
    uint16_t transitionMs = 700;
    int16_t  presetId   = -1;

    // Множитель от планировщика (fade in/out). Итоговая яркость на ленте =
    // brightness * scheduleEnvelope. Отделён специально, чтобы плавное
    // угасание по расписанию не затирало заданное пользователем значение.
    float scheduleEnvelope = 1.0f;

    Segment segments[MAX_SEGMENTS];

    uint8_t effectiveBrightness() const {
        if (!on) return 0;
        float v = brightness * scheduleEnvelope;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v + 0.5f);
    }
};

// Доступ к состоянию под мьютексом.
class StateLock {
   public:
    StateLock();
    ~StateLock();
    AppState* operator->();
    AppState& operator*();

   private:
    bool acquired_ = false;
};

// Инициализация мьютекса и сегмента по умолчанию.
void stateInit(uint16_t ledCount);

// Снимок без блокировки — для мест, где важнее не встать в очередь
// (например, отдача /json/state). Копия дешёвая.
AppState stateSnapshot();

// Пометить, что состояние изменилось: разбудить рендер, толкнуть WebSocket,
// поставить конфиг в очередь на сохранение.
void stateTouch();
bool stateConsumeDirty();

// Телеметрия рендера. Пишется контроллером ленты с ядра рендера, читается
// веб-обработчиками — поэтому лежит здесь, а не в LedController: до него
// из web_server.cpp не дотянуться, он живёт объектом в main.cpp.
void statsPublish(uint16_t fps, uint16_t milliamps);
uint16_t statsFps();
uint16_t statsMilliamps();

}  // namespace lumen
