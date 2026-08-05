// web_server.cpp — HTTP + WebSocket.
//
// /json/* повторяет схему WLED: именно её читает нативная интеграция Home
// Assistant, приложения WLED и сторонние инструменты. Свои возможности
// (расписание) живут в /api/*, чтобы не ломать совместимость.
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "config.h"
#include "core/presets.h"
#include "core/scheduler_runner.h"
#include "core/state.h"
#include "led/effects.h"
#include "net.h"
#include "store/store.h"

namespace lumen {
namespace {

AsyncWebServer   g_server(80);
AsyncWebSocket   g_ws("/ws");

// Проверка пароля на изменяющих ручках /api/*.
//
// Пустой пароль означает «защиты нет» — так работает первый запуск и так же
// живёт большинство домашних установок. WLED-совместимые /json/* не
// закрываются никогда: интеграция Home Assistant про пароли не знает, и
// закрыв их, мы бы сломали ровно то, ради чего затевалась совместимость.
bool authOk(AsyncWebServerRequest* req) {
    const String& pass = config().apiPassword;
    if (pass.isEmpty()) return true;
    if (req->authenticate("lumen", pass.c_str())) return true;
    req->requestAuthentication();
    return false;
}

uint8_t clampIndex(int v, uint8_t count) {
    if (v < 0) return 0;
    return v >= count ? static_cast<uint8_t>(count - 1) : static_cast<uint8_t>(v);
}

void segmentToJson(const Segment& seg, uint8_t id, JsonObject o) {
    o["id"]    = id;
    o["start"] = seg.start;
    o["stop"]  = seg.stop;
    o["len"]   = seg.length();
    o["on"]    = seg.on;
    o["bri"]   = seg.bri;

    JsonArray col = o["col"].to<JsonArray>();
    JsonArray c0 = col.add<JsonArray>();
    c0.add(seg.primary.r); c0.add(seg.primary.g); c0.add(seg.primary.b);
    JsonArray c1 = col.add<JsonArray>();
    c1.add(seg.secondary.r); c1.add(seg.secondary.g); c1.add(seg.secondary.b);
    JsonArray c2 = col.add<JsonArray>();
    c2.add(0); c2.add(0); c2.add(0);

    o["fx"]  = seg.fx;
    o["sx"]  = seg.speed;
    o["ix"]  = seg.intensity;
    o["pal"] = seg.palette;
    o["rev"] = seg.reverse;
    o["sel"] = true;
}

void stateToJson(const AppState& st, JsonObject root) {
    root["on"]         = st.on;
    root["bri"]        = st.brightness;
    root["transition"] = st.transitionMs / 100;  // WLED считает в 0.1 с
    root["ps"]         = st.presetId;
    root["pl"]         = -1;

    // Планировщик Lumen — своё поле, HA его игнорирует.
    root["lumenEnv"] = st.scheduleEnvelope;

    JsonArray segs = root["seg"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_SEGMENTS; ++i) {
        if (!st.segments[i].active) continue;
        segmentToJson(st.segments[i], i, segs.add<JsonObject>());
    }
}

void infoToJson(JsonObject root) {
    // Интеграция HA требует версию 0.14.0 или новее — рапортуем совместимую,
    // а собственный номер отдаём отдельным полем.
    root["ver"] = WLED_COMPAT_VERSION;
    root["vid"] = 2405190;
    root["lumen"] = LUMEN_VERSION;
    root["board"] = LUMEN_BOARD;

    JsonObject leds = root["leds"].to<JsonObject>();
    leds["count"]  = config().ledCount;
    // pwr — оценка потребления в мА, её показывают приложения WLED.
    // maxpwr = 0 означает «ограничение не задано»: лимита тока у нас нет.
    leds["pwr"]    = statsMilliamps();
    leds["maxpwr"] = 0;
    leds["maxseg"] = MAX_SEGMENTS;
    leds["fps"]    = statsFps();

    root["name"]    = config().name;
    root["udpport"] = 21324;
    root["live"]    = false;
    root["fxcount"]  = effectCount();
    root["palcount"] = paletteCount();
    root["arch"]     = "esp32";
    root["core"]     = ESP.getSdkVersion();
    root["freeheap"] = ESP.getFreeHeap();
    root["uptime"]   = millis() / 1000;
    root["brand"]    = "Lumen";
    root["product"]  = "FOSS";
    root["mac"]      = WiFi.macAddress();
    root["ip"]       = WiFi.localIP().toString();
}

void effectsToJson(JsonArray arr) {
    for (uint8_t i = 0; i < effectCount(); ++i) arr.add(effectName(i));
}

void palettesToJson(JsonArray arr) {
    for (uint8_t i = 0; i < paletteCount(); ++i) arr.add(paletteName(i));
}

// ------------------------------------------------- применение входящего JSON

void applySegment(JsonObjectConst o, Segment& seg, uint16_t ledCount) {
    if (!o["start"].isNull()) seg.start = o["start"].as<uint16_t>();
    if (!o["stop"].isNull())  seg.stop  = o["stop"].as<uint16_t>();
    if (seg.stop > ledCount) seg.stop = ledCount;
    if (seg.start > seg.stop) seg.start = seg.stop;

    if (!o["on"].isNull())  seg.on  = o["on"].as<bool>();
    if (!o["bri"].isNull()) seg.bri = o["bri"].as<uint8_t>();
    if (!o["fx"].isNull()) seg.fx = clampIndex(o["fx"].as<int>(), effectCount());
    if (!o["sx"].isNull())  seg.speed     = o["sx"].as<uint8_t>();
    if (!o["ix"].isNull())  seg.intensity = o["ix"].as<uint8_t>();
    if (!o["pal"].isNull()) {
        seg.palette = clampIndex(o["pal"].as<int>(), paletteCount());
    }
    if (!o["rev"].isNull()) seg.reverse = o["rev"].as<bool>();

    JsonArrayConst col = o["col"].as<JsonArrayConst>();
    if (!col.isNull()) {
        uint8_t slot = 0;
        for (JsonArrayConst c : col) {
            if (c.size() < 3) { slot++; continue; }
            Rgb v{c[0].as<uint8_t>(), c[1].as<uint8_t>(), c[2].as<uint8_t>()};
            if (slot == 0) seg.primary = v;
            else if (slot == 1) seg.secondary = v;
            slot++;
        }
    }
}

void applyState(JsonObjectConst root) {
    StateLock st;
    const uint16_t ledCount = config().ledCount;

    if (!root["on"].isNull()) st->on = root["on"].as<bool>();
    if (!root["bri"].isNull()) st->brightness = root["bri"].as<uint8_t>();
    if (!root["transition"].isNull()) {
        st->transitionMs = root["transition"].as<uint16_t>() * 100;
    }

    JsonVariantConst segVar = root["seg"];
    if (segVar.is<JsonArrayConst>()) {
        for (JsonObjectConst o : segVar.as<JsonArrayConst>()) {
            const int id = o["id"] | 0;
            if (id < 0 || id >= MAX_SEGMENTS) continue;
            st->segments[id].active = true;
            applySegment(o, st->segments[id], ledCount);
        }
    } else if (segVar.is<JsonObjectConst>()) {
        // WLED допускает и одиночный объект — HA этим пользуется.
        JsonObjectConst o = segVar.as<JsonObjectConst>();
        const int id = o["id"] | 0;
        if (id >= 0 && id < MAX_SEGMENTS) {
            st->segments[id].active = true;
            applySegment(o, st->segments[id], ledCount);
        }
    }

}

// Пресеты обрабатываем ДО захвата мьютекса состояния: presetApply берёт
// тот же мьютекс, а он не рекурсивный — иначе тихий дедлок.
void applyIncoming(JsonObjectConst root) {
    const int psave = root["psave"] | -1;
    if (psave > 0) {
        presetSave(psave, root["n"] | "");
        return;
    }

    const int ps = root["ps"] | -1;
    if (ps > 0) presetApply(ps);

    applyState(root);
}

// --------------------------------------------------------------- WebSocket

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
               AwsEventType type, void*, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        JsonDocument doc;
        stateToJson(stateSnapshot(), doc.to<JsonObject>());
        String out;
        serializeJson(doc, out);
        client->text(out);
        return;
    }

    if (type == WS_EVT_DATA) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) return;
        applyIncoming(doc.as<JsonObjectConst>());
        stateTouch();
        requestStateSave();
        webBroadcastState();
    }
}

// ---------------------------------------------------------- временная страница

const char kPlaceholder[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lumen</title>
<style>
 :root{color-scheme:dark}
 body{margin:0;min-height:100vh;display:grid;place-items:center;
      background:#12100f;color:#efe7dc;
      font:16px/1.6 ui-sans-serif,system-ui,sans-serif}
 main{width:min(420px,90vw);padding:24px}
 h1{font-size:22px;margin:0 0 6px}
 p{color:#8f857a;font-size:14px;margin:0 0 20px}
 code{background:#1c1917;border:1px solid #322c27;border-radius:6px;
      padding:2px 6px;font-size:13px}
 ul{padding-left:18px;margin:0}
 li{margin:6px 0;font-size:14px}
</style></head><body><main>
<h1>Lumen работает</h1>
<p>Интерфейс не найден на файловой системе. Залейте его командой
<code>pio run -t uploadfs</code>. Пока управление доступно через API.</p>
<ul>
 <li><code>GET /json</code> — состояние, эффекты, палитры</li>
 <li><code>POST /json/state</code> — управление</li>
 <li><code>GET /api/schedule</code> — расписание</li>
 <li><code>WS /ws</code> — состояние в реальном времени</li>
</ul>
</main></body></html>)HTML";

}  // namespace

// ------------------------------------------------------------------ публичное

void webBroadcastState() {
    if (g_ws.count() == 0) return;
    JsonDocument doc;
    stateToJson(stateSnapshot(), doc.to<JsonObject>());
    String out;
    serializeJson(doc, out);
    g_ws.textAll(out);
}

void webServerBegin() {
    g_ws.onEvent(onWsEvent);
    g_server.addHandler(&g_ws);

    // -------- WLED-совместимое чтение

    g_server.on("/json", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* res = req->beginResponseStream("application/json");
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        stateToJson(stateSnapshot(), root["state"].to<JsonObject>());
        infoToJson(root["info"].to<JsonObject>());
        effectsToJson(root["effects"].to<JsonArray>());
        palettesToJson(root["palettes"].to<JsonArray>());
        serializeJson(doc, *res);
        req->send(res);
    });

    g_server.on("/json/state", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* res = req->beginResponseStream("application/json");
        JsonDocument doc;
        stateToJson(stateSnapshot(), doc.to<JsonObject>());
        serializeJson(doc, *res);
        req->send(res);
    });

    g_server.on("/json/info", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* res = req->beginResponseStream("application/json");
        JsonDocument doc;
        infoToJson(doc.to<JsonObject>());
        serializeJson(doc, *res);
        req->send(res);
    });

    g_server.on("/json/eff", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* res = req->beginResponseStream("application/json");
        JsonDocument doc;
        effectsToJson(doc.to<JsonArray>());
        serializeJson(doc, *res);
        req->send(res);
    });

    g_server.on("/json/pal", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* res = req->beginResponseStream("application/json");
        JsonDocument doc;
        palettesToJson(doc.to<JsonArray>());
        serializeJson(doc, *res);
        req->send(res);
    });

    // -------- своё: градиенты палитр для превью
    //
    // /json/pal остаётся WLED-совместимым списком имён — его читают чужие
    // приложения. Опорные точки живут отдельно, чтобы интерфейс рисовал
    // превью по данным прошивки, а не по своей копии палитр.

    g_server.on("/api/palettes", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* res = req->beginResponseStream("application/json");
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (uint8_t i = 0; i < paletteCount(); ++i) {
            JsonObject o = arr.add<JsonObject>();
            o["n"] = paletteName(i);
            JsonArray stops = o["stops"].to<JsonArray>();

            auto addStop = [&stops](uint8_t p, uint8_t r, uint8_t g, uint8_t b) {
                JsonArray a = stops.add<JsonArray>();
                a.add(p); a.add(r); a.add(g); a.add(b);
            };

            // «Rainbow» считается по кругу HSV и опорных точек не хранит —
            // для превью синтезируем их здесь. Пустой массив stops означает
            // «взять текущий цвет сегмента», это случай «Default».
            if (paletteStopCount(i) == 0 &&
                strcmp(paletteName(i), "Rainbow") == 0) {
                addStop(0, 255, 0, 0);     addStop(42, 255, 255, 0);
                addStop(85, 0, 255, 0);    addStop(128, 0, 255, 255);
                addStop(170, 0, 0, 255);   addStop(213, 255, 0, 255);
                addStop(255, 255, 0, 0);
                continue;
            }

            for (uint8_t k = 0; k < paletteStopCount(i); ++k) {
                const PaletteStopInfo s = paletteStopAt(i, k);
                addStop(s.pos, s.r, s.g, s.b);
            }
        }

        serializeJson(doc, *res);
        req->send(res);
    });

    // -------- WLED-совместимая запись

    auto* stateHandler = new AsyncCallbackJsonWebHandler(
        "/json/state", [](AsyncWebServerRequest* req, JsonVariant& json) {
            applyIncoming(json.as<JsonObjectConst>());
            stateTouch();
            requestStateSave();
            webBroadcastState();
            req->send(200, "application/json", "{\"success\":true}");
        });
    g_server.addHandler(stateHandler);

    auto* rootHandler = new AsyncCallbackJsonWebHandler(
        "/json", [](AsyncWebServerRequest* req, JsonVariant& json) {
            JsonObjectConst root = json.as<JsonObjectConst>();
            // HA умеет слать и «плоский» state, и обёртку {"state":{...}}.
            applyIncoming(root["state"].isNull()
                              ? root
                              : root["state"].as<JsonObjectConst>());
            stateTouch();
            requestStateSave();
            webBroadcastState();
            req->send(200, "application/json", "{\"success\":true}");
        });
    g_server.addHandler(rootHandler);

    // -------- своё: расписание

    g_server.on("/api/schedule", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", scheduleToJson());
    });

    auto* schedHandler = new AsyncCallbackJsonWebHandler(
        "/api/schedule", [](AsyncWebServerRequest* req, JsonVariant& json) {
            if (!authOk(req)) return;
            String body;
            serializeJson(json, body);
            String err;
            if (!scheduleFromJson(body, err)) {
                JsonDocument doc;
                doc["error"] = err;
                String out;
                serializeJson(doc, out);
                req->send(400, "application/json", out);
                return;
            }
            saveSchedule();
            req->send(200, "application/json", "{\"success\":true}");
        });
    g_server.addHandler(schedHandler);

    // -------- пресеты

    g_server.on("/presets.json", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", presetsJson());
    });

    g_server.on("/api/presets", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", presetsIndexJson());
    });

    g_server.on("/api/presets", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (!authOk(req)) return;
        if (!req->hasParam("id")) {
            req->send(400, "application/json", "{\"error\":\"нужен id\"}");
            return;
        }
        const int id = req->getParam("id")->value().toInt();
        const bool ok = presetDelete(id);
        req->send(ok ? 200 : 404, "application/json",
                  ok ? "{\"success\":true}" : "{\"error\":\"не найден\"}");
    });

    // -------- своё: статус планировщика

    g_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", schedulerStatusJson());
    });

    // -------- своё: конфигурация

    g_server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["name"]    = config().name;
        doc["ssid"]    = config().wifiSsid;   // пароль наружу не отдаём
        doc["ledCount"] = config().ledCount;
        doc["ledPin"]   = config().ledPin;
        doc["bootBri"]  = config().bootBrightness;
        doc["lat"]      = config().latitude;
        doc["lon"]      = config().longitude;
        doc["tz"]       = config().timezone;
        doc["maxLeds"]  = MAX_LEDS;
        doc["maxSeg"]   = MAX_SEGMENTS;
        // Сам пароль наружу не отдаём — только факт, что он задан.
        doc["authSet"]  = !config().apiPassword.isEmpty();
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    auto* configHandler = new AsyncCallbackJsonWebHandler(
        "/api/config", [](AsyncWebServerRequest* req, JsonVariant& json) {
            if (!authOk(req)) return;
            JsonObjectConst o = json.as<JsonObjectConst>();
            bool needsReboot = false;

            if (!o["name"].isNull()) config().name = o["name"].as<String>();
            if (!o["ssid"].isNull()) {
                config().wifiSsid = o["ssid"].as<String>();
                needsReboot = true;
            }
            if (!o["pass"].isNull()) {
                config().wifiPass = o["pass"].as<String>();
                needsReboot = true;
            }
            if (!o["ledCount"].isNull()) {
                const int n = o["ledCount"].as<int>();
                if (n > 0 && n <= MAX_LEDS) {
                    config().ledCount = n;
                    needsReboot = true;  // буферы кадра выделяются на старте
                }
            }
            if (!o["ledPin"].isNull()) {
                config().ledPin = o["ledPin"].as<uint8_t>();
                needsReboot = true;
            }
            if (!o["bootBri"].isNull()) {
                config().bootBrightness = o["bootBri"].as<uint8_t>();
            }
            if (!o["lat"].isNull()) config().latitude = o["lat"].as<double>();
            if (!o["lon"].isNull()) config().longitude = o["lon"].as<double>();
            if (!o["tz"].isNull()) {
                config().timezone = o["tz"].as<String>();
                needsReboot = true;  // TZ применяется при configTzTime
            }
            if (!o["apiPass"].isNull()) {
                config().apiPassword = o["apiPass"].as<String>();
                needsReboot = true;  // пароль OTA ставится при старте
            }

            saveConfig();

            JsonDocument res;
            res["success"] = true;
            res["reboot"]  = needsReboot;
            String out;
            serializeJson(res, out);
            req->send(200, "application/json", out);
        });
    g_server.addHandler(configHandler);

    g_server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!authOk(req)) return;
        req->send(200, "application/json", "{\"success\":true}");
        req->onDisconnect([]() { ESP.restart(); });
    });

    // -------- бэкап и восстановление

    g_server.on("/api/backup", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Собираем один файл, который целиком описывает устройство:
        // расписание, сцены и текущий свет. Настройки самого устройства
        // (Wi-Fi, пины, координаты) сюда намеренно не попадают — бэкап
        // переносится между устройствами, а они у каждого свои.
        String out = "{\"lumen\":\"" LUMEN_VERSION "\",\"schedule\":";
        out += scheduleToJson();
        out += ",\"presets\":";
        out += presetsJson();
        out += ",\"state\":";
        out += stateStoreToJson();
        out += "}";
        auto* res = req->beginResponse(200, "application/json", out);
        res->addHeader("Content-Disposition",
                       "attachment; filename=\"lumen-backup.json\"");
        req->send(res);
    });

    auto* restoreHandler = new AsyncCallbackJsonWebHandler(
        "/api/backup", [](AsyncWebServerRequest* req, JsonVariant& json) {
            if (!authOk(req)) return;
            JsonObjectConst root = json.as<JsonObjectConst>();
            String err;

            if (!root["schedule"].isNull()) {
                String body;
                serializeJson(root["schedule"], body);
                if (!scheduleFromJson(body, err)) {
                    req->send(400, "application/json",
                              String("{\"error\":\"расписание: ") + err + "\"}");
                    return;
                }
                saveSchedule();
            }

            if (!root["presets"].isNull()) {
                String body;
                serializeJson(root["presets"], body);
                if (!presetsReplace(body, err)) {
                    req->send(400, "application/json",
                              String("{\"error\":\"пресеты: ") + err + "\"}");
                    return;
                }
            }

            // Состояние восстанавливаем последним: пресеты к этому моменту
            // уже на месте, и сцена, на которую ссылается presetId, есть.
            if (!root["state"].isNull()) {
                String body;
                serializeJson(root["state"], body);
                if (!stateStoreFromJson(body, err)) {
                    req->send(400, "application/json",
                              String("{\"error\":\"состояние: ") + err + "\"}");
                    return;
                }
                saveState();
                stateTouch();
                webBroadcastState();
            }

            req->send(200, "application/json", "{\"success\":true}");
        });
    g_server.addHandler(restoreHandler);

    // -------- статика и заглушка

    if (LittleFS.exists("/www/index.html")) {
        g_server.serveStatic("/", LittleFS, "/www/")
            .setDefaultFile("index.html");
    } else {
        g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200, "text/html; charset=utf-8", kPlaceholder);
        });
    }

    g_server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"error\":\"not found\"}");
    });

    g_server.begin();
    log_i("HTTP-сервер запущен на :80");
}

}  // namespace lumen
