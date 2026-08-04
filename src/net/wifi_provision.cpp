#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "config.h"
#include "net.h"
#include "store/store.h"

namespace lumen {
namespace {

// Портал живёт в собственном экземпляре сервера и в собственном режиме
// работы: смешивать его с рабочим API на одном порту — источник тонких багов.
AsyncWebServer* g_portal = nullptr;
DNSServer*      g_dns    = nullptr;

String apSsid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "Lumen-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

const char kPortalPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lumen — подключение</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; min-height:100vh; display:grid; place-items:center;
         background:#12100f; color:#efe7dc;
         font:16px/1.5 ui-sans-serif,system-ui,sans-serif; }
  main { width:min(360px,90vw); padding:28px 24px; }
  h1 { font-size:20px; margin:0 0 4px; letter-spacing:-.01em; }
  p.sub { margin:0 0 24px; color:#8f857a; font-size:14px; }
  label { display:block; font-size:13px; color:#8f857a; margin:16px 0 6px; }
  input { width:100%; box-sizing:border-box; padding:11px 12px;
          background:#1c1917; border:1px solid #322c27; border-radius:8px;
          color:#efe7dc; font-size:15px; }
  input:focus { outline:2px solid #c8873c; outline-offset:1px;
                border-color:transparent; }
  button { width:100%; margin-top:24px; padding:12px; border:0;
           border-radius:8px; background:#c8873c; color:#12100f;
           font-size:15px; font-weight:600; cursor:pointer; }
  button:hover { background:#d9954a; }
</style></head><body><main>
<h1>Подключить Lumen к сети</h1>
<p class="sub">Контроллер перезапустится и появится в вашей сети под именем
   lumen.local</p>
<form method="POST" action="/wifi">
  <label for="s">Имя сети Wi-Fi</label>
  <input id="s" name="ssid" autocomplete="off" required>
  <label for="p">Пароль</label>
  <input id="p" name="pass" type="password" autocomplete="off">
  <button type="submit">Сохранить и перезапустить</button>
</form>
</main></body></html>)HTML";

void startPortal() {
    WiFi.mode(WIFI_AP);
    const String ssid = apSsid();
    WiFi.softAP(ssid.c_str());

    g_dns = new DNSServer();
    g_dns->start(53, "*", WiFi.softAPIP());

    g_portal = new AsyncWebServer(80);

    g_portal->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html; charset=utf-8", kPortalPage);
    });

    g_portal->on("/wifi", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("ssid", true)) {
            req->send(400, "text/plain; charset=utf-8",
                      "Укажите имя сети");
            return;
        }
        config().wifiSsid = req->getParam("ssid", true)->value();
        config().wifiPass = req->hasParam("pass", true)
                                ? req->getParam("pass", true)->value()
                                : "";
        saveConfig();
        req->send(200, "text/html; charset=utf-8",
                  "<meta charset='utf-8'><body style='font-family:sans-serif'>"
                  "Настройки сохранены. Перезапускаю…</body>");
        delay(400);
        ESP.restart();
    });

    // Любой другой адрес возвращает портал — так его открывает
    // системный экран «войти в сеть» на телефоне.
    g_portal->onNotFound([](AsyncWebServerRequest* req) {
        req->send(200, "text/html; charset=utf-8", kPortalPage);
    });

    g_portal->begin();
    log_i("Портал настройки поднят: сеть %s, адрес %s", ssid.c_str(),
          WiFi.softAPIP().toString().c_str());
}

}  // namespace

bool netConnectOrProvision() {
    if (config().wifiSsid.isEmpty()) {
        log_w("Сеть не настроена — поднимаю портал");
        startPortal();
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // иначе на C3 растёт задержка HTTP-ответов
    WiFi.setHostname(config().name.c_str());
    WiFi.begin(config().wifiSsid.c_str(), config().wifiPass.c_str());

    const uint32_t deadline = millis() + kWifiConnectTimeoutMs;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(200);
    }

    if (WiFi.status() != WL_CONNECTED) {
        log_w("Не удалось подключиться к «%s» — поднимаю портал",
              config().wifiSsid.c_str());
        startPortal();
        return false;
    }

    // Автопереподключение: без этого после перезагрузки роутера
    // устройство остаётся оффлайн до собственного ребута.
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    log_i("Подключено: %s, RSSI %d dBm", WiFi.localIP().toString().c_str(),
          WiFi.RSSI());
    return true;
}

void netTick() {
    if (g_dns) g_dns->processNextRequest();
}

}  // namespace lumen
