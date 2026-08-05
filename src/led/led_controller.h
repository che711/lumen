// led_controller.h — вывод на ленту.
//
// Отдельный буфер кадра (не пишем в NeoPixelBus напрямую) нужен, чтобы
// поверх результата эффекта накладывать глобальную яркость и огибающую
// планировщика, плавно переходить между сценами, а уже потом — гамму
// с дизерингом.
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

    // Отпечаток «внешнего вида» сегментов: fx, палитра, цвета, границы.
    // Скорость и интенсивность в него не входят намеренно — их крутят
    // слайдером, и перезапуск перехода на каждое движение выглядел бы
    // как залипание. Глобальная яркость тоже: она накладывается позже.
    static uint32_t lookFingerprint(const AppState& state);

    uint16_t ledCount_ = 0;
    Rgb*     frame_    = nullptr;   // результат эффектов, до яркости
    Rgb*     prev_     = nullptr;   // кадр до смены сцены, для перехода
    void*    strip_    = nullptr;   // NeoPixelBus, спрятан за void* ради заголовка

    uint32_t lookFp_ = 0;
    bool     haveLook_ = false;
    uint32_t transitionStartMs_ = 0;
    uint16_t transitionMs_ = 0;     // 0 — перехода нет

    uint32_t frameSeq_ = 0;         // монотонный счётчик кадров — фаза дизеринга
    uint32_t frameCount_ = 0;
    uint32_t fpsWindowMs_ = 0;
    uint16_t fps_ = 0;
    uint16_t lastMilliamps_ = 0;    // оценка потребления последнего кадра
};

}  // namespace lumen
