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

// Ток одного кристалла WS2812 на полной яркости, мА.
constexpr uint32_t kMilliampsPerChannel = 20;

inline uint8_t blend8(uint8_t a, uint8_t b, uint8_t f) {
    return static_cast<uint8_t>((a * (255 - f) + b * f) / 255);
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

    // Буфер перехода не критичен: без него просто не будет плавной смены
    // сцены, а лента продолжит работать.
    prev_ = new (std::nothrow) Rgb[ledCount_]();
    if (!prev_) log_w("Нет памяти на буфер перехода — смена сцены будет резкой");

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

uint32_t LedController::lookFingerprint(const AppState& state) {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };

    for (uint8_t s = 0; s < MAX_SEGMENTS; ++s) {
        const Segment& g = state.segments[s];
        if (!g.active) { mix(0xFFFFFFFFu); continue; }
        mix(g.fx);
        mix(g.palette);
        mix(g.on ? 1u : 0u);
        mix(g.bri);
        mix(g.start);
        mix(g.stop);
        mix(g.reverse ? 1u : 0u);
        mix((static_cast<uint32_t>(g.primary.r) << 16) |
            (static_cast<uint32_t>(g.primary.g) << 8) | g.primary.b);
        mix((static_cast<uint32_t>(g.secondary.r) << 16) |
            (static_cast<uint32_t>(g.secondary.g) << 8) | g.secondary.b);
    }
    return h;
}

void LedController::render(const AppState& state, uint32_t nowMs) {
    if (!strip_ || !frame_) return;

    // Смена сцены: запоминаем последний показанный кадр и запускаем переход.
    // Делать это надо до очистки frame_ — там ещё лежит предыдущий кадр.
    const uint32_t fp = lookFingerprint(state);
    if (fp != lookFp_) {
        if (haveLook_ && prev_ && state.transitionMs > 0) {
            memcpy(prev_, frame_, sizeof(Rgb) * ledCount_);
            transitionStartMs_ = nowMs;
            transitionMs_ = state.transitionMs;
        }
        lookFp_ = fp;
        haveLook_ = true;
    }

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

    // Переход между сценами: подмешиваем сохранённый кадр к новому.
    // Смешивание идёт до глобальной яркости и гаммы — иначе на низкой
    // яркости переход шёл бы ступенями.
    if (transitionMs_ && prev_) {
        const uint32_t elapsed = nowMs - transitionStartMs_;
        if (elapsed >= transitionMs_) {
            transitionMs_ = 0;
        } else {
            const uint8_t f =
                static_cast<uint8_t>(elapsed * 255 / transitionMs_);
            for (uint16_t i = 0; i < ledCount_; ++i) {
                frame_[i] = Rgb{blend8(prev_[i].r, frame_[i].r, f),
                                blend8(prev_[i].g, frame_[i].g, f),
                                blend8(prev_[i].b, frame_[i].b, f)};
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

    // Публикуем телеметрию: до контроллера из web_server.cpp не дотянуться.
    statsPublish(fps_, lastMilliamps_);
}

void LedController::applyAndShow(uint8_t globalBrightness) {
    Strip* strip = asStrip(strip_);
    if (!strip->CanShow()) return;

    uint32_t sum = 0;

    for (uint16_t i = 0; i < ledCount_; ++i) {
        const Rgb& c = frame_[i];
        // Яркость применяется до гаммы — так слайдер получается перцептивно
        // линейным. Точность нижней части шкалы вытягивает 16-битная таблица
        // с дизерингом на выходе.
        const uint8_t r = quantize(gamma16(scale8(c.r, globalBrightness)), i, frameSeq_);
        const uint8_t g = quantize(gamma16(scale8(c.g, globalBrightness)), i, frameSeq_);
        const uint8_t b = quantize(gamma16(scale8(c.b, globalBrightness)), i, frameSeq_);
        sum += static_cast<uint32_t>(r) + g + b;
        strip->SetPixelColor(i, RgbColor(r, g, b));
    }
    strip->Show();

    // Оценка потребления: каждый кристалл на полной яркости — около 20 мА,
    // плюс примерно 1 мА покоя на диод. Это прикидка для интерфейса, а не
    // измерение: реальный ток зависит от партии ленты и просадки на проводах.
    const uint32_t mA = sum * kMilliampsPerChannel / 255 + ledCount_;
    lastMilliamps_ = mA > 65535 ? 65535 : static_cast<uint16_t>(mA);
}

void LedController::blackout() {
    if (!strip_) return;
    Strip* strip = asStrip(strip_);
    strip->ClearTo(RgbColor(0));
    strip->Show();
}

}  // namespace lumen
