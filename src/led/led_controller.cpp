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
uint8_t gamma8(uint8_t v) {
    static uint8_t table[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; ++i) {
            table[i] = static_cast<uint8_t>(
                powf(i / 255.0f, 2.2f) * 255.0f + 0.5f);
        }
        ready = true;
    }
    return table[v];
}

}  // namespace

bool LedController::begin(uint16_t ledCount, uint8_t pin) {
    if (ledCount == 0 || ledCount > MAX_LEDS) ledCount = MAX_LEDS;
    ledCount_ = ledCount;

    frame_ = new (std::nothrow) Rgb[ledCount_]();
    shown_ = new (std::nothrow) Rgb[ledCount_]();
    if (!frame_ || !shown_) {
        log_e("Не хватило памяти на буферы кадра для %u диодов", ledCount_);
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
        const Rgb out{gamma8(scale8(c.r, globalBrightness)),
                      gamma8(scale8(c.g, globalBrightness)),
                      gamma8(scale8(c.b, globalBrightness))};
        shown_[i] = out;
        strip->SetPixelColor(i, RgbColor(out.r, out.g, out.b));
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
