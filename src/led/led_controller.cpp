#include "led_controller.h"

#include <NeoPixelBus.h>

#include <math.h>

#include <new>

#include "config.h"
#include "effects.h"

namespace lumen {
namespace {

// RMT работает и на C3, и на classic ESP32, и не конфликтует с Wi-Fi
// (в отличие от bit-bang методов, которые ловят джиттер от прерываний).
using Strip = NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod>;

inline Strip* asStrip(void* p) { return static_cast<Strip*>(p); }

inline uint8_t scale8(uint8_t value, uint8_t factor) {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * factor) >> 8);
}

// Гамма-коррекция: линейный ШИМ выглядит для глаза резким на низкой яркости,
// а диммирование по расписанию живёт именно там.
//
// Таблица намеренно 16-битная. В восьми битах gamma(v) обращается в ноль при
// v < 18 — то есть нижние 7% шкалы были бы сплошной чернотой, и получасовой
// fadeIn первые минуты не показывал бы ничего. Дробную часть забирает
// дизеринг ниже.
uint16_t gamma16(uint8_t v) {
    static uint16_t table[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; ++i) {
            table[i] = static_cast<uint16_t>(
                powf(i / 255.0f, 2.2f) * 65535.0f + 0.5f);
        }
        ready = true;
    }
    return table[v];
}

// Упорядоченный дизеринг по кадрам и по позиции: уровень тоньше одной ступени
// ШИМ превращается в мерцание, которое глаз усредняет. Порог гуляет и во
// времени, и вдоль ленты — иначе на однотонной заливке проступают полосы.
inline uint8_t ditherThreshold(uint16_t index, uint32_t frame) {
    static const uint8_t kBayer[4] = {0, 128, 64, 192};
    return kBayer[(index + frame) & 3];
}

// Сводит 16-битное значение к восьми битам, отдавая остаток дизерингу.
inline uint8_t quantize(uint16_t value, uint16_t index, uint32_t frame) {
    const uint8_t high = static_cast<uint8_t>(value >> 8);
    if (high == 255) return 255;
    const uint8_t frac = static_cast<uint8_t>(value & 0xFF);
    return frac > ditherThreshold(index, frame) ? high + 1 : high;
}

}  // namespace

bool LedController::begin(uint16_t ledCount, uint8_t pin) {
    if (ledCount == 0 || ledCount > MAX_LEDS) ledCount = MAX_LEDS;
    ledCount_ = ledCount;

    frame_ = new (std::nothrow) Rgb[ledCount_]();
    if (!frame_) {
        log_e("Не хватило памяти на буфер кадра для %u диодов", ledCount_);
        return false;
    }

    Strip* strip = new (std::nothrow) Strip(ledCount_, pin);
    if (!strip) return false;
    strip->Begin();
    strip->ClearTo(RgbColor(0));
    strip->Show();
    strip_ = strip;

    log_i("Лента: %u диодов на GPIO%u", ledCount_, pin);
    return true;
}

bool LedController::canShow() const {
    return strip_ && asStrip(strip_)->CanShow();
}

void LedController::render(const AppState& state, uint32_t nowMs) {
    if (!strip_ || !frame_) return;

    // Чистим кадр: диоды вне активных сегментов должны гаснуть.
    for (uint16_t i = 0; i < ledCount_; ++i) frame_[i] = Rgb{0, 0, 0};

    for (uint8_t s = 0; s < MAX_SEGMENTS; ++s) {
        const Segment& seg = state.segments[s];
        if (!seg.active || !seg.on) continue;

        const uint16_t start = seg.start < ledCount_ ? seg.start : ledCount_;
        const uint16_t stop  = seg.stop < ledCount_ ? seg.stop : ledCount_;
        if (stop <= start) continue;

        const uint16_t len = stop - start;

        EffectContext ctx;
        ctx.buffer    = frame_ + start;
        ctx.length    = len;
        ctx.timeMs    = nowMs;
        ctx.speed     = seg.speed;
        ctx.intensity = seg.intensity;
        ctx.palette   = seg.palette;
        ctx.primary   = seg.primary;
        ctx.secondary = seg.secondary;

        renderEffect(seg.fx, ctx);

        // Яркость сегмента применяем сразу, глобальную — позже.
        if (seg.bri < 255) {
            for (uint16_t i = 0; i < len; ++i) {
                ctx.buffer[i] = Rgb{scale8(ctx.buffer[i].r, seg.bri),
                                    scale8(ctx.buffer[i].g, seg.bri),
                                    scale8(ctx.buffer[i].b, seg.bri)};
            }
        }

        if (seg.reverse) {
            for (uint16_t i = 0; i < len / 2; ++i) {
                Rgb tmp = ctx.buffer[i];
                ctx.buffer[i] = ctx.buffer[len - 1 - i];
                ctx.buffer[len - 1 - i] = tmp;
            }
        }
    }

    applyAndShow(state.effectiveBrightness());

    // Монотонный счётчик кадров — фаза дизеринга. Отдельно от frameCount_,
    // который обнуляется каждую секунду ради подсчёта FPS.
    frameSeq_++;

    // Счётчик FPS — попадает в /json/info, полезен для диагностики.
    frameCount_++;
    if (nowMs - fpsWindowMs_ >= 1000) {
        fps_ = frameCount_;
        frameCount_ = 0;
        fpsWindowMs_ = nowMs;
    }
}

void LedController::applyAndShow(uint8_t globalBrightness) {
    Strip* strip = asStrip(strip_);
    if (!strip->CanShow()) return;

    for (uint16_t i = 0; i < ledCount_; ++i) {
        const Rgb& c = frame_[i];
        // Яркость применяется до гаммы — так слайдер получается перцептивно
        // линейным. Точность нижней части шкалы вытягивает 16-битная таблица
        // с дизерингом на выходе.
        const uint8_t r = quantize(gamma16(scale8(c.r, globalBrightness)), i, frameSeq_);
        const uint8_t g = quantize(gamma16(scale8(c.g, globalBrightness)), i, frameSeq_);
        const uint8_t b = quantize(gamma16(scale8(c.b, globalBrightness)), i, frameSeq_);
        strip->SetPixelColor(i, RgbColor(r, g, b));
    }
    strip->Show();
}

void LedController::blackout() {
    if (!strip_) return;
    Strip* strip = asStrip(strip_);
    strip->ClearTo(RgbColor(0));
    strip->Show();
}

}  // namespace lumen
