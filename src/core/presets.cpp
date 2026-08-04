#include "presets.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "config.h"
#include "led/effects.h"
#include "state.h"

namespace lumen {
namespace {

bool readAll(JsonDocument& doc) {
    File f = LittleFS.open(kPresetsPath, "r");
    if (!f) {
        doc.to<JsonObject>();
        return false;
    }
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        log_e("presets.json повреждён: %s", err.c_str());
        doc.to<JsonObject>();
        return false;
    }
    return true;
}

bool writeAll(const JsonDocument& doc) {
    File f = LittleFS.open(kPresetsPath, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

void segmentToPresetJson(const Segment& seg, uint8_t id, JsonObject o) {
    o["id"]    = id;
    o["start"] = seg.start;
    o["stop"]  = seg.stop;
    o["on"]    = seg.on;
    o["bri"]   = seg.bri;
    o["fx"]    = seg.fx;
    o["sx"]    = seg.speed;
    o["ix"]    = seg.intensity;
    o["pal"]   = seg.palette;
    o["rev"]   = seg.reverse;

    JsonArray col = o["col"].to<JsonArray>();
    JsonArray c0 = col.add<JsonArray>();
    c0.add(seg.primary.r); c0.add(seg.primary.g); c0.add(seg.primary.b);
    JsonArray c1 = col.add<JsonArray>();
    c1.add(seg.secondary.r); c1.add(seg.secondary.g); c1.add(seg.secondary.b);
}

uint8_t clampIdx(int v, uint8_t count) {
    if (v < 0) return 0;
    return v >= count ? static_cast<uint8_t>(count - 1) : static_cast<uint8_t>(v);
}

}  // namespace

bool presetApply(int id) {
    if (id <= 0) return false;

    JsonDocument doc;
    if (!readAll(doc)) return false;

    char key[8];
    snprintf(key, sizeof(key), "%d", id);
    JsonObjectConst p = doc[key];
    if (p.isNull()) {
        log_w("Пресет %d не найден", id);
        return false;
    }

    StateLock st;

    if (!p["on"].isNull())  st->on = p["on"].as<bool>();
    if (!p["bri"].isNull()) st->brightness = p["bri"].as<uint8_t>();

    // Сегменты, которых нет в пресете, гасим: иначе остатки предыдущей
    // сцены протекают в новую и получается «а почему тут ещё светится».
    bool touched[MAX_SEGMENTS] = {false};

    for (JsonObjectConst o : p["seg"].as<JsonArrayConst>()) {
        const int sid = o["id"] | 0;
        if (sid < 0 || sid >= MAX_SEGMENTS) continue;

        Segment& seg = st->segments[sid];
        seg.active = true;
        touched[sid] = true;

        if (!o["start"].isNull()) seg.start = o["start"].as<uint16_t>();
        if (!o["stop"].isNull())  seg.stop  = o["stop"].as<uint16_t>();
        if (!o["on"].isNull())    seg.on    = o["on"].as<bool>();
        if (!o["bri"].isNull())   seg.bri   = o["bri"].as<uint8_t>();
        if (!o["fx"].isNull())    seg.fx    = clampIdx(o["fx"], effectCount());
        if (!o["sx"].isNull())    seg.speed = o["sx"].as<uint8_t>();
        if (!o["ix"].isNull())    seg.intensity = o["ix"].as<uint8_t>();
        if (!o["pal"].isNull())   seg.palette = clampIdx(o["pal"], paletteCount());
        if (!o["rev"].isNull())   seg.reverse = o["rev"].as<bool>();

        JsonArrayConst col = o["col"].as<JsonArrayConst>();
        uint8_t slot = 0;
        for (JsonArrayConst c : col) {
            if (c.size() >= 3) {
                Rgb v{c[0].as<uint8_t>(), c[1].as<uint8_t>(), c[2].as<uint8_t>()};
                if (slot == 0) seg.primary = v;
                else if (slot == 1) seg.secondary = v;
            }
            slot++;
        }
    }

    for (uint8_t i = 0; i < MAX_SEGMENTS; ++i) {
        if (st->segments[i].active && !touched[i]) st->segments[i].on = false;
    }

    st->presetId = id;
    log_i("Применён пресет %d", id);
    return true;
}

bool presetSave(int id, const String& name) {
    if (id <= 0) return false;

    JsonDocument doc;
    readAll(doc);  // отсутствие файла — не ошибка, создадим

    const AppState snap = stateSnapshot();

    char key[8];
    snprintf(key, sizeof(key), "%d", id);

    JsonObject p = doc[key].to<JsonObject>();
    p["n"]   = name.isEmpty() ? String("Пресет ") + id : name;
    p["on"]  = snap.on;
    p["bri"] = snap.brightness;

    JsonArray segs = p["seg"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_SEGMENTS; ++i) {
        if (!snap.segments[i].active) continue;
        segmentToPresetJson(snap.segments[i], i, segs.add<JsonObject>());
    }

    if (!writeAll(doc)) return false;
    log_i("Сохранён пресет %d", id);
    return true;
}

bool presetDelete(int id) {
    JsonDocument doc;
    if (!readAll(doc)) return false;

    char key[8];
    snprintf(key, sizeof(key), "%d", id);
    if (doc[key].isNull()) return false;

    doc.remove(key);
    return writeAll(doc);
}

String presetsJson() {
    File f = LittleFS.open(kPresetsPath, "r");
    if (!f) return "{}";
    String out = f.readString();
    f.close();
    return out.isEmpty() ? "{}" : out;
}

bool presetsReplace(const String& json, String& errorOut) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        errorOut = String("Не удалось разобрать JSON: ") + err.c_str();
        return false;
    }
    if (!doc.is<JsonObject>()) {
        errorOut = "Ожидается объект, где ключ — номер пресета";
        return false;
    }
    return writeAll(doc);
}

String presetsIndexJson() {
    JsonDocument src;
    readAll(src);

    JsonDocument out;
    JsonArray arr = out.to<JsonArray>();
    for (JsonPairConst kv : src.as<JsonObjectConst>()) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = atoi(kv.key().c_str());
        o["n"]  = kv.value()["n"] | "";
    }

    String s;
    serializeJson(out, s);
    return s;
}

}  // namespace lumen
