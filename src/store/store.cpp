#include "store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "config.h"
#include "core/state.h"

namespace lumen {
namespace {

DeviceConfig      g_config;
std::vector<Rule> g_rules;

bool     g_savePending = false;
uint32_t g_saveDueAt   = 0;

bool     g_stateSavePending = false;
uint32_t g_stateSaveDueAt   = 0;

const char* anchorTypeName(AnchorType t) {
    switch (t) {
        case AnchorType::Sunrise: return "sunrise";
        case AnchorType::Sunset:  return "sunset";
        default:                  return "clock";
    }
}

AnchorType anchorTypeFromName(const char* s) {
    if (!s) return AnchorType::Clock;
    if (strcmp(s, "sunrise") == 0) return AnchorType::Sunrise;
    if (strcmp(s, "sunset") == 0)  return AnchorType::Sunset;
    return AnchorType::Clock;
}

void anchorToJson(const Anchor& a, JsonObject obj) {
    obj["type"] = anchorTypeName(a.type);
    obj["value"] = a.value;
}

Anchor anchorFromJson(JsonObjectConst obj) {
    Anchor a;
    a.type  = anchorTypeFromName(obj["type"] | "clock");
    a.value = obj["value"] | 0;
    return a;
}

}  // namespace

bool storeBegin() {
    if (!LittleFS.begin(true)) {
        log_e("LittleFS не смонтирован");
        return false;
    }
    loadConfig();
    loadSchedule();
    return true;
}

DeviceConfig& config() { return g_config; }
std::vector<Rule>& scheduleRules() { return g_rules; }

// ------------------------------------------------------------------ конфиг

bool loadConfig() {
    File f = LittleFS.open(kConfigPath, "r");
    if (!f) {
        log_w("config.json отсутствует, используются значения по умолчанию");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        log_e("config.json повреждён: %s", err.c_str());
        return false;
    }

    g_config.name     = doc["name"] | g_config.name;
    g_config.wifiSsid = doc["wifi"]["ssid"] | "";
    g_config.wifiPass = doc["wifi"]["pass"] | "";
    g_config.apiPassword = doc["apiPass"] | "";
    g_config.ledCount = doc["led"]["count"] | g_config.ledCount;
    g_config.ledPin   = doc["led"]["pin"] | g_config.ledPin;
    g_config.bootBrightness = doc["led"]["bootBri"] | g_config.bootBrightness;
    g_config.latitude  = doc["loc"]["lat"] | g_config.latitude;
    g_config.longitude = doc["loc"]["lon"] | g_config.longitude;
    g_config.timezone  = doc["loc"]["tz"] | g_config.timezone;

    log_i("Конфиг загружен: %s, %u диодов", g_config.name.c_str(),
          g_config.ledCount);
    return true;
}

bool saveConfig() {
    JsonDocument doc;
    doc["name"] = g_config.name;
    doc["wifi"]["ssid"] = g_config.wifiSsid;
    doc["wifi"]["pass"] = g_config.wifiPass;
    doc["apiPass"]      = g_config.apiPassword;
    doc["led"]["count"] = g_config.ledCount;
    doc["led"]["pin"]   = g_config.ledPin;
    doc["led"]["bootBri"] = g_config.bootBrightness;
    doc["loc"]["lat"] = g_config.latitude;
    doc["loc"]["lon"] = g_config.longitude;
    doc["loc"]["tz"]  = g_config.timezone;

    File f = LittleFS.open(kConfigPath, "w");
    if (!f) return false;
    serializeJsonPretty(doc, f);
    f.close();
    return true;
}

void requestConfigSave() {
    g_savePending = true;
    g_saveDueAt   = millis() + kConfigWriteDebounceMs;
}

void storeTick(uint32_t nowMs) {
    if (g_savePending && static_cast<int32_t>(nowMs - g_saveDueAt) >= 0) {
        g_savePending = false;
        saveConfig();
    }
    if (g_stateSavePending &&
        static_cast<int32_t>(nowMs - g_stateSaveDueAt) >= 0) {
        g_stateSavePending = false;
        saveState();
    }
}

// ------------------------------------------------------------- состояние

void requestStateSave() {
    g_stateSavePending = true;
    g_stateSaveDueAt   = millis() + kConfigWriteDebounceMs;
}

String stateStoreToJson() {
    JsonDocument doc;

    {
        StateLock st;
        doc["on"]         = st->on;
        doc["bri"]        = st->brightness;
        doc["ps"]         = st->presetId;
        doc["transition"] = st->transitionMs;

        JsonArray segs = doc["seg"].to<JsonArray>();
        for (uint8_t i = 0; i < MAX_SEGMENTS; ++i) {
            const Segment& s = st->segments[i];
            if (!s.active) continue;

            JsonObject o = segs.add<JsonObject>();
            o["id"]    = i;
            o["start"] = s.start;
            o["stop"]  = s.stop;
            o["on"]    = s.on;
            o["bri"]   = s.bri;
            o["fx"]    = s.fx;
            o["sx"]    = s.speed;
            o["ix"]    = s.intensity;
            o["pal"]   = s.palette;
            o["rev"]   = s.reverse;

            JsonArray c0 = o["c0"].to<JsonArray>();
            c0.add(s.primary.r); c0.add(s.primary.g); c0.add(s.primary.b);
            JsonArray c1 = o["c1"].to<JsonArray>();
            c1.add(s.secondary.r); c1.add(s.secondary.g); c1.add(s.secondary.b);
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}

bool stateStoreFromJson(const String& json, String& errorOut) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        errorOut = String("Не удалось разобрать JSON: ") + err.c_str();
        return false;
    }
    if (!doc.is<JsonObject>()) {
        errorOut = "Ожидается объект состояния";
        return false;
    }

    const uint16_t ledCount = g_config.ledCount;

    StateLock st;
    st->on           = doc["on"] | true;
    st->brightness   = doc["bri"] | g_config.bootBrightness;
    st->presetId     = doc["ps"] | -1;
    st->transitionMs = doc["transition"] | st->transitionMs;

    // Огибающую намеренно не восстанавливаем: планировщик посчитает её сам
    // на первом тике, вместе с позицией внутри фейда.
    st->scheduleEnvelope = 1.0f;

    for (JsonObjectConst o : doc["seg"].as<JsonArrayConst>()) {
        const int id = o["id"] | -1;
        if (id < 0 || id >= MAX_SEGMENTS) continue;

        Segment& s = st->segments[id];
        s.active = true;
        s.start  = o["start"] | 0;
        s.stop   = o["stop"] | 0;

        // Лента могла стать короче с прошлого запуска — иначе сегмент
        // остался бы висеть за пределами буфера.
        if (s.stop > ledCount) s.stop = ledCount;
        if (s.start > s.stop) s.start = s.stop;

        s.on        = o["on"] | true;
        s.bri       = o["bri"] | 255;
        // fx и pal не клампим: renderEffect и paletteColor делают это сами,
        // а откат прошивки не должен ронять загрузку.
        s.fx        = o["fx"] | 0;
        s.speed     = o["sx"] | 128;
        s.intensity = o["ix"] | 128;
        s.palette   = o["pal"] | 0;
        s.reverse   = o["rev"] | false;

        JsonArrayConst c0 = o["c0"].as<JsonArrayConst>();
        if (c0.size() >= 3) {
            s.primary = Rgb{c0[0].as<uint8_t>(), c0[1].as<uint8_t>(),
                            c0[2].as<uint8_t>()};
        }
        JsonArrayConst c1 = o["c1"].as<JsonArrayConst>();
        if (c1.size() >= 3) {
            s.secondary = Rgb{c1[0].as<uint8_t>(), c1[1].as<uint8_t>(),
                              c1[2].as<uint8_t>()};
        }
    }

    log_i("Состояние восстановлено: яркость %u", st->brightness);
    return true;
}

bool saveState() {
    File f = LittleFS.open(kStatePath, "w");
    if (!f) {
        log_e("Не удалось открыть %s на запись", kStatePath);
        return false;
    }
    f.print(stateStoreToJson());
    f.close();
    return true;
}

bool loadState() {
    File f = LittleFS.open(kStatePath, "r");
    if (!f) {
        log_i("state.json отсутствует — первый запуск");
        return false;
    }
    String json = f.readString();
    f.close();

    String err;
    if (!stateStoreFromJson(json, err)) {
        log_e("state.json: %s", err.c_str());
        return false;
    }
    return true;
}

// -------------------------------------------------------------- расписание

String scheduleToJson() {
    JsonDocument doc;
    JsonArray arr = doc["rules"].to<JsonArray>();

    for (const Rule& r : g_rules) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]       = r.id;
        o["enabled"]  = r.enabled;
        o["priority"] = r.priority;
        anchorToJson(r.from, o["from"].to<JsonObject>());
        anchorToJson(r.to, o["to"].to<JsonObject>());
        o["days"] = r.days;

        if (!r.except.empty()) {
            JsonArray ex = o["except"].to<JsonArray>();
            for (const Date& d : r.except) {
                char buf[11];
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month,
                         d.day);
                ex.add(buf);
            }
        }

        JsonObject act = o["action"].to<JsonObject>();
        act["preset"]   = r.action.preset;
        act["bri"]      = r.action.brightness;
        act["powerOff"] = r.action.powerOff;

        o["fadeIn"]  = r.fadeInSec;
        o["fadeOut"] = r.fadeOutSec;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

bool scheduleFromJson(const String& json, String& errorOut) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        errorOut = String("Не удалось разобрать JSON: ") + err.c_str();
        return false;
    }

    JsonArrayConst arr = doc["rules"].as<JsonArrayConst>();
    if (arr.isNull()) {
        errorOut = "Ожидается объект с массивом rules";
        return false;
    }

    std::vector<Rule> parsed;
    for (JsonObjectConst o : arr) {
        Rule r;
        r.id       = o["id"] | 0;
        r.enabled  = o["enabled"] | true;
        r.priority = o["priority"] | 0;
        r.from     = anchorFromJson(o["from"]);
        r.to       = anchorFromJson(o["to"]);
        r.days     = o["days"] | Days::All;

        for (JsonVariantConst v : o["except"].as<JsonArrayConst>()) {
            const char* s = v.as<const char*>();
            Date d;
            if (s && sscanf(s, "%d-%d-%d", &d.year, &d.month, &d.day) == 3) {
                r.except.push_back(d);
            }
        }

        JsonObjectConst act = o["action"];
        r.action.preset     = act["preset"] | -1;
        r.action.brightness = act["bri"] | -1;
        r.action.powerOff   = act["powerOff"] | false;

        r.fadeInSec  = o["fadeIn"] | 0;
        r.fadeOutSec = o["fadeOut"] | 0;

        if (r.id == 0) {
            errorOut = "У каждого правила должен быть ненулевой id";
            return false;
        }
        parsed.push_back(r);
    }

    g_rules = std::move(parsed);
    return true;
}

bool loadSchedule() {
    File f = LittleFS.open(kSchedulePath, "r");
    if (!f) {
        log_i("schedule.json отсутствует — расписание пустое");
        return false;
    }
    String json = f.readString();
    f.close();

    String err;
    if (!scheduleFromJson(json, err)) {
        log_e("schedule.json: %s", err.c_str());
        return false;
    }
    log_i("Расписание загружено: %u правил", g_rules.size());
    return true;
}

bool saveSchedule() {
    File f = LittleFS.open(kSchedulePath, "w");
    if (!f) return false;
    f.print(scheduleToJson());
    f.close();
    return true;
}

}  // namespace lumen
