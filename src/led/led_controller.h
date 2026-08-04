// led_controller.h — вывод на ленту.
//
// Отдельный буфер кадра (не пишем в NeoPixelBus напрямую) нужен, чтобы
// поверх результата эффекта накладывать глобальную яркость и огибающую
// планировщика, а также плавно переходить между кадрами при смене пресета.
#pragma once

#include <Arduino.h>

#include "core/state.h"

namespace lumen {

class LedController {
   public:
    bool begin(uint16_t ledCount, uint8_t pin);

    // Отрисовать один кадр. Вызывать не чаще kTargetFps.
    void render(const AppState& state, uint32_t nowMs);

    // Ленту физически можно обновлять только когда закончилась предыдущая
    // передача по RMT — иначе кадр порвётся.
    bool canShow() const;

    uint16_t ledCount() const { return ledCount_; }
    uint16_t lastFps() const { return fps_; }

    // Мгновенно погасить (используется при уходе в портал настройки).
    void blackout();

   private:
    void applyAndShow(uint8_t globalBrightness);

    uint16_t ledCount_ = 0;
    Rgb*     frame_    = nullptr;   // результат эффектов, до яркости
    Rgb*     shown_    = nullptr;   // то, что реально ушло на ленту
    void*    strip_    = nullptr;   // NeoPixelBus, спрятан за void* ради заголовка

    uint32_t lastShowMs_ = 0;
    uint32_t frameCount_ = 0;
    uint32_t fpsWindowMs_ = 0;
    uint16_t fps_ = 0;
};

}  // namespace lumen
