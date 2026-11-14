// layout_json.h — разбор и запись раскладки диодов в JSON.
//
// Формат тот же, что отдаёт редактор раскладки:
//   {"v":1,"count":N,"leds":[[x,y],...]}
// Точки идут по порядку провода, координаты 0..1000. Здесь только работа с
// ArduinoJson, без Arduino и файловой системы, поэтому заголовок собирается
// и на хосте.
#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <vector>

#include "layout.h"

namespace lumen {

// Разбирает раскладку в out. Координаты зажимаются в 0..kCoordMax. При ошибке
// out не меняется, а err указывает на статическую строку.
inline bool layoutParse(JsonVariantConst root, size_t maxPoints,
                        std::vector<layout::Point>& out, const char*& err) {
    if (!root.is<JsonObjectConst>()) {
        err = "Ожидается объект раскладки";
        return false;
    }
    JsonArrayConst leds = root["leds"].as<JsonArrayConst>();
    if (leds.isNull()) {
        err = "Нет массива leds";
        return false;
    }
    if (leds.size() > maxPoints) {
        err = "Слишком много диодов";
        return false;
    }

    auto clampCoord = [](float v) -> uint16_t {
        if (!(v > 0.0f)) return 0;  // и отрицательные, и NaN
        if (v > layout::kCoordMax) return layout::kCoordMax;
        return static_cast<uint16_t>(std::lround(v));
    };

    std::vector<layout::Point> pts;
    pts.reserve(leds.size());
    for (JsonVariantConst v : leds) {
        JsonArrayConst p = v.as<JsonArrayConst>();
        if (p.isNull() || p.size() < 2 || !p[0].is<float>() || !p[1].is<float>()) {
            err = "Диод должен быть парой [x, y]";
            return false;
        }
        pts.push_back(layout::Point{clampCoord(p[0].as<float>()),
                                    clampCoord(p[1].as<float>())});
    }

    out.swap(pts);
    return true;
}

inline void layoutWrite(const std::vector<layout::Point>& pts, JsonObject o) {
    o["v"] = 1;
    o["count"] = pts.size();
    JsonArray arr = o["leds"].to<JsonArray>();
    for (const layout::Point& p : pts) {
        JsonArray xy = arr.add<JsonArray>();
        xy.add(p.x);
        xy.add(p.y);
    }
}

}  // namespace lumen
