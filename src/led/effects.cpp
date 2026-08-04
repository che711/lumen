#include "effects.h"

#include <math.h>

namespace lumen {
namespace {

// ------------------------------------------------------------- палитры

struct PaletteStop {
    uint8_t pos;
    uint8_t r, g, b;
};

// Каждая палитра — набор опорных точек с линейной интерполяцией между ними.
const PaletteStop kParty[]  = {{0, 255, 0, 128}, {64, 255, 100, 0},
                               {128, 255, 220, 0}, {192, 180, 0, 255},
                               {255, 255, 0, 128}};
const PaletteStop kOcean[]  = {{0, 0, 20, 90}, {80, 0, 90, 160},
                               {160, 0, 170, 190}, {255, 190, 240, 255}};
const PaletteStop kForest[] = {{0, 0, 40, 10}, {90, 20, 110, 20},
                               {170, 90, 160, 30}, {255, 200, 220, 90}};
const PaletteStop kLava[]   = {{0, 20, 0, 0}, {90, 160, 20, 0},
                               {180, 255, 110, 0}, {255, 255, 230, 120}};

// Градиенты ниже написаны с нуля. Из WLED ничего не копируется: он под
// EUPL-1.2, и заимствование потребовало бы сменить лицензию всего проекта.
const PaletteStop kCloud[]  = {{0, 0, 0, 80}, {60, 0, 60, 160},
                               {130, 120, 160, 220}, {200, 220, 235, 255},
                               {255, 255, 255, 255}};
const PaletteStop kHeat[]   = {{0, 0, 0, 0}, {64, 140, 0, 0},
                               {128, 220, 60, 0}, {190, 255, 190, 0},
                               {255, 255, 255, 220}};
const PaletteStop kSunset[] = {{0, 60, 10, 60}, {70, 200, 50, 60},
                               {140, 255, 120, 40}, {200, 255, 190, 90},
                               {255, 255, 235, 180}};
const PaletteStop kAurora[] = {{0, 0, 20, 40}, {70, 0, 140, 110},
                               {140, 40, 220, 150}, {200, 120, 90, 220},
                               {255, 20, 20, 90}};
const PaletteStop kIce[]    = {{0, 0, 10, 60}, {80, 0, 90, 180},
                               {160, 120, 200, 240}, {255, 255, 255, 255}};
const PaletteStop kCandle[] = {{0, 60, 10, 0}, {90, 180, 60, 0},
                               {170, 255, 140, 30}, {255, 255, 200, 120}};
const PaletteStop kAutumn[] = {{0, 60, 15, 0}, {80, 170, 60, 0},
                               {160, 220, 140, 20}, {220, 200, 90, 10},
                               {255, 120, 30, 0}};
const PaletteStop kSakura[] = {{0, 120, 20, 60}, {80, 255, 120, 180},
                               {160, 255, 190, 215}, {255, 255, 240, 245}};
const PaletteStop kNeon[]   = {{0, 0, 255, 200}, {85, 80, 0, 255},
                               {170, 255, 0, 150}, {255, 0, 255, 200}};
const PaletteStop kDeep[]   = {{0, 0, 0, 30}, {90, 0, 40, 110},
                               {170, 0, 120, 150}, {255, 80, 220, 200}};

struct PaletteDef {
    const char*        name;
    const PaletteStop* stops;
    uint8_t            count;
};

// ВНИМАНИЕ: порядок = индекс pal в API. Только добавлять в конец.
const PaletteDef kPalettes[] = {
    {"Default", nullptr, 0},          // берёт primary сегмента
    {"Rainbow", nullptr, 0},          // спецслучай: HSV по кругу
    {"Party", kParty, 5},
    {"Ocean", kOcean, 4},
    {"Forest", kForest, 4},
    {"Lava", kLava, 4},
    {"Cloud", kCloud, 5},
    {"Heat", kHeat, 5},
    {"Sunset", kSunset, 5},
    {"Aurora", kAurora, 5},
    {"Ice", kIce, 4},
    {"Candle", kCandle, 4},
    {"Autumn", kAutumn, 5},
    {"Sakura", kSakura, 4},
    {"Neon", kNeon, 4},
    {"Deep", kDeep, 4},
};

constexpr uint8_t kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

Rgb hsvToRgb(uint8_t hue, uint8_t sat, uint8_t val) {
    const uint8_t region = hue / 43;
    const uint8_t rem    = (hue - region * 43) * 6;
    const uint8_t p = (val * (255 - sat)) >> 8;
    const uint8_t q = (val * (255 - ((sat * rem) >> 8))) >> 8;
    const uint8_t t = (val * (255 - ((sat * (255 - rem)) >> 8))) >> 8;

    switch (region) {
        case 0:  return Rgb{val, t, p};
        case 1:  return Rgb{q, val, p};
        case 2:  return Rgb{p, val, t};
        case 3:  return Rgb{p, q, val};
        case 4:  return Rgb{t, p, val};
        default: return Rgb{val, p, q};
    }
}

Rgb scale(const Rgb& c, uint8_t factor) {
    return Rgb{static_cast<uint8_t>((c.r * factor) >> 8),
               static_cast<uint8_t>((c.g * factor) >> 8),
               static_cast<uint8_t>((c.b * factor) >> 8)};
}

// Детерминированный псевдослучайный «шум» — одинаковый на каждом кадре
// для одного и того же (index, seed). Позволяет держать эффекты чистыми.
uint8_t hashNoise(uint16_t index, uint16_t seed) {
    uint32_t h = index * 2654435761u ^ seed * 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return static_cast<uint8_t>(h);
}

// Скорость: 0..255 → множитель времени. Подобрано так, чтобы 128 давало
// «спокойное» движение, а 255 — быстрое, но ещё не мельтешение.
inline uint32_t scaledTime(uint32_t timeMs, uint8_t speed) {
    return static_cast<uint32_t>(timeMs * (1 + speed / 16));
}

// ------------------------------------------------------------- эффекты

void fxSolid(EffectContext& ctx) {
    for (uint16_t i = 0; i < ctx.length; ++i) {
        ctx.buffer[i] = paletteColor(
            ctx.palette,
            ctx.length > 1 ? static_cast<uint8_t>(i * 255 / (ctx.length - 1)) : 0,
            ctx.primary);
    }
}

void fxBlink(EffectContext& ctx) {
    const uint32_t period = 2000 - ctx.speed * 7;
    const bool phase = (ctx.timeMs % period) < (period * ctx.intensity / 255);
    const Rgb color = phase ? ctx.primary : ctx.secondary;
    for (uint16_t i = 0; i < ctx.length; ++i) ctx.buffer[i] = color;
}

void fxBreathe(EffectContext& ctx) {
    const float period = 6000.0f - ctx.speed * 20.0f;
    const float phase = fmodf(ctx.timeMs, period) / period;
    // Синус в квадрате даёт восприятие «дыхания» ровнее чистого синуса.
    const float s = sinf(phase * PI);
    const uint8_t level = static_cast<uint8_t>(s * s * 255.0f);
    for (uint16_t i = 0; i < ctx.length; ++i) {
        const uint8_t pos =
            ctx.length > 1 ? static_cast<uint8_t>(i * 255 / (ctx.length - 1)) : 0;
        ctx.buffer[i] = scale(paletteColor(ctx.palette, pos, ctx.primary), level);
    }
}

void fxWipe(EffectContext& ctx) {
    if (ctx.length == 0) return;
    const uint32_t period = 8000 - ctx.speed * 28;
    const float phase = static_cast<float>(ctx.timeMs % period) / period;
    const uint16_t head = static_cast<uint16_t>(phase * ctx.length * 2);
    const bool erasing = head >= ctx.length;
    const uint16_t edge = erasing ? head - ctx.length : head;

    for (uint16_t i = 0; i < ctx.length; ++i) {
        const uint8_t pos = static_cast<uint8_t>(i * 255 / ctx.length);
        const Rgb lit = paletteColor(ctx.palette, pos, ctx.primary);
        const bool filled = erasing ? (i >= edge) : (i < edge);
        ctx.buffer[i] = filled ? lit : ctx.secondary;
    }
}

void fxRainbow(EffectContext& ctx) {
    const uint32_t t = scaledTime(ctx.timeMs, ctx.speed) / 40;
    // intensity управляет «плотностью» радуги вдоль ленты
    const uint16_t spread = 1 + (ctx.intensity >> 3);
    for (uint16_t i = 0; i < ctx.length; ++i) {
        const uint8_t hue = static_cast<uint8_t>((i * spread + t) & 0xFF);
        ctx.buffer[i] = hsvToRgb(hue, 255, 255);
    }
}

void fxTwinkle(EffectContext& ctx) {
    const uint32_t period = 4000 - ctx.speed * 14;
    const uint32_t generation = ctx.timeMs / period;
    const uint32_t withinMs = ctx.timeMs % period;
    // Плавное появление и угасание внутри поколения.
    const float phase = static_cast<float>(withinMs) / period;
    const uint8_t level = static_cast<uint8_t>(sinf(phase * PI) * 255.0f);

    const uint8_t threshold = 255 - ctx.intensity / 2;

    for (uint16_t i = 0; i < ctx.length; ++i) {
        ctx.buffer[i] = ctx.secondary;
        if (hashNoise(i, generation) > threshold) {
            const uint8_t pos = hashNoise(i, generation + 7777);
            ctx.buffer[i] = scale(paletteColor(ctx.palette, pos, ctx.primary),
                                  level);
        }
    }
}

void fxChase(EffectContext& ctx) {
    if (ctx.length == 0) return;
    const uint32_t t = scaledTime(ctx.timeMs, ctx.speed) / 60;
    // intensity — доля ленты, занятая «поездом».
    const uint16_t blockLen = 1 + (ctx.length * ctx.intensity) / 1024;
    const uint16_t head = t % ctx.length;

    for (uint16_t i = 0; i < ctx.length; ++i) {
        const uint16_t rel = (i + ctx.length - head) % ctx.length;
        const uint8_t pos = static_cast<uint8_t>(i * 255 / ctx.length);
        ctx.buffer[i] = rel < blockLen
                            ? paletteColor(ctx.palette, pos, ctx.primary)
                            : ctx.secondary;
    }
}

void fxComet(EffectContext& ctx) {
    if (ctx.length == 0) return;
    const uint32_t t = scaledTime(ctx.timeMs, ctx.speed) / 50;
    const uint16_t head = t % ctx.length;
    // Длина хвоста от intensity; экспоненциальное затухание выглядит
    // естественнее линейного — глаз воспринимает яркость логарифмически.
    const float decay = 0.75f + (ctx.intensity / 255.0f) * 0.22f;

    for (uint16_t i = 0; i < ctx.length; ++i) {
        const uint16_t behind = (head + ctx.length - i) % ctx.length;
        float level = 1.0f;
        for (uint16_t k = 0; k < behind && level > 0.004f; ++k) level *= decay;
        const uint8_t pos = static_cast<uint8_t>(i * 255 / ctx.length);
        const Rgb base = paletteColor(ctx.palette, pos, ctx.primary);
        ctx.buffer[i] = scale(base, static_cast<uint8_t>(level * 255.0f));
    }
}

void fxFire(EffectContext& ctx) {
    if (ctx.length == 0) return;
    const uint32_t period = 260 - ctx.speed / 2;   // время жизни «поколения»
    const uint32_t gen = ctx.timeMs / period;
    const float mix = static_cast<float>(ctx.timeMs % period) / period;

    for (uint16_t i = 0; i < ctx.length; ++i) {
        // Интерполяция между двумя поколениями шума даёт мерцание без
        // хранения состояния между кадрами.
        const float a = hashNoise(i, gen) / 255.0f;
        const float b = hashNoise(i, gen + 1) / 255.0f;
        float heat = a * (1.0f - mix) + b * mix;

        // Основание ленты горячее верхушки.
        const float base = 1.0f - static_cast<float>(i) / ctx.length * 0.55f;
        heat = heat * (0.45f + 0.55f * (ctx.intensity / 255.0f)) * base;
        if (heat > 1.0f) heat = 1.0f;

        const uint8_t h = static_cast<uint8_t>(heat * 255.0f);
        if (ctx.palette == 0) {
            // «Чёрное тело»: красный → оранжевый → белый.
            ctx.buffer[i] = Rgb{h,
                                static_cast<uint8_t>(h > 128 ? (h - 128) * 2 : 0),
                                static_cast<uint8_t>(h > 200 ? (h - 200) * 4 : 0)};
        } else {
            ctx.buffer[i] = scale(paletteColor(ctx.palette, h, ctx.primary), h);
        }
    }
}

void fxScanner(EffectContext& ctx) {
    if (ctx.length == 0) return;
    const uint32_t period = 6000 - ctx.speed * 21;
    const float phase = static_cast<float>(ctx.timeMs % period) / period;
    // Треугольная волна: глаз ждёт от «глаза Ларсона» разворота, а не скачка.
    const float tri = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    const float head = tri * (ctx.length - 1);
    const float width = 1.0f + (ctx.intensity / 255.0f) * (ctx.length / 6.0f);

    for (uint16_t i = 0; i < ctx.length; ++i) {
        const float d = fabsf(static_cast<float>(i) - head) / width;
        const float level = d >= 1.0f ? 0.0f : (1.0f - d) * (1.0f - d);
        const uint8_t pos = static_cast<uint8_t>(i * 255 / ctx.length);
        const Rgb base = paletteColor(ctx.palette, pos, ctx.primary);
        ctx.buffer[i] = level > 0.0f
                            ? scale(base, static_cast<uint8_t>(level * 255.0f))
                            : ctx.secondary;
    }
}

void fxTheater(EffectContext& ctx) {
    if (ctx.length == 0) return;
    const uint32_t period = 900 - ctx.speed * 3;
    const uint8_t gap = 3;
    const uint32_t step = (ctx.timeMs / period) % gap;

    for (uint16_t i = 0; i < ctx.length; ++i) {
        const uint8_t pos = static_cast<uint8_t>(i * 255 / ctx.length);
        ctx.buffer[i] = (i % gap == step)
                            ? paletteColor(ctx.palette, pos, ctx.primary)
                            : ctx.secondary;
    }
}

void fxColorloop(EffectContext& ctx) {
    const uint32_t t = scaledTime(ctx.timeMs, ctx.speed) / 120;
    const uint8_t hue = static_cast<uint8_t>(t & 0xFF);
    // intensity задаёт насыщенность: на минимуме получается мягкий белый.
    const uint8_t sat = 80 + (ctx.intensity * 175) / 255;
    const Rgb color = hsvToRgb(hue, sat, 255);
    for (uint16_t i = 0; i < ctx.length; ++i) ctx.buffer[i] = color;
}

struct EffectDef {
    const char* name;
    EffectFn    fn;
};

// ВНИМАНИЕ: порядок = индекс fx в API. Только добавлять в конец.
const EffectDef kEffects[] = {
    {"Solid", fxSolid},
    {"Blink", fxBlink},
    {"Breathe", fxBreathe},
    {"Wipe", fxWipe},
    {"Rainbow", fxRainbow},
    {"Twinkle", fxTwinkle},
    {"Chase", fxChase},
    {"Comet", fxComet},
    {"Fire", fxFire},
    {"Scanner", fxScanner},
    {"Theater", fxTheater},
    {"Colorloop", fxColorloop},
};

constexpr uint8_t kEffectCount = sizeof(kEffects) / sizeof(kEffects[0]);

}  // namespace

// ------------------------------------------------------------- публичное

uint8_t effectCount() { return kEffectCount; }

const char* effectName(uint8_t index) {
    return index < kEffectCount ? kEffects[index].name : "Solid";
}

void renderEffect(uint8_t index, EffectContext& ctx) {
    if (ctx.buffer == nullptr || ctx.length == 0) return;
    if (index >= kEffectCount) index = 0;
    kEffects[index].fn(ctx);
}

uint8_t paletteCount() { return kPaletteCount; }

const char* paletteName(uint8_t index) {
    return index < kPaletteCount ? kPalettes[index].name : "Default";
}

// Опорные точки наружу — чтобы интерфейс рисовал превью по данным прошивки,
// а не по своей копии палитр. Копия рано или поздно разъезжается с оригиналом.
uint8_t paletteStopCount(uint8_t palette) {
    return palette < kPaletteCount ? kPalettes[palette].count : 0;
}

PaletteStopInfo paletteStopAt(uint8_t palette, uint8_t index) {
    if (palette >= kPaletteCount || index >= kPalettes[palette].count ||
        kPalettes[palette].stops == nullptr) {
        return PaletteStopInfo{0, 0, 0, 0};
    }
    const PaletteStop& s = kPalettes[palette].stops[index];
    return PaletteStopInfo{s.pos, s.r, s.g, s.b};
}

Rgb paletteColor(uint8_t palette, uint8_t pos, const Rgb& primary) {
    if (palette >= kPaletteCount || kPalettes[palette].stops == nullptr) {
        if (palette == 1) return hsvToRgb(pos, 255, 255);  // Rainbow
        return primary;                                     // Default
    }

    const PaletteDef& def = kPalettes[palette];
    for (uint8_t i = 0; i + 1 < def.count; ++i) {
        const PaletteStop& a = def.stops[i];
        const PaletteStop& b = def.stops[i + 1];
        if (pos >= a.pos && pos <= b.pos) {
            const uint16_t span = b.pos - a.pos;
            if (span == 0) return Rgb{a.r, a.g, a.b};
            const uint16_t f = ((pos - a.pos) * 255) / span;
            auto mix = [f](uint8_t x, uint8_t y) {
                return static_cast<uint8_t>((x * (255 - f) + y * f) / 255);
            };
            return Rgb{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
        }
    }
    const PaletteStop& last = def.stops[def.count - 1];
    return Rgb{last.r, last.g, last.b};
}

}  // namespace lumen
