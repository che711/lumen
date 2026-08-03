#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"
#include "net.h"
#include "store/store.h"

namespace lumen {
namespace {

bool g_started = false;

void advertise(const String& hostname) {
    MDNS.addService("http", "tcp", 80);

    // Ключевая строчка всего проекта: манифест интеграции WLED в Home
    // Assistant матчит zeroconf-сервис `_wled._tcp.local.`. Анонсируя его,
    // мы получаем автообнаружение, а вместе с ним — мобильные приложения
    // WLED и сторонние инструменты, без единой строки на стороне HA.
    MDNS.addService("wled", "tcp", 80);
    MDNS.addServiceTxt("wled", "tcp", "mac", WiFi.macAddress());
    MDNS.addServiceTxt("wled", "tcp", "name", config().name);
    MDNS.addServiceTxt("wled", "tcp", "version", WLED_COMPAT_VERSION);

    log_i("mDNS: http://%s.local (сервис _wled._tcp)", hostname.c_str());
}

}  // namespace

void discoveryBegin(const String& hostname) {
    if (!MDNS.begin(hostname.c_str())) {
        log_e("mDNS не стартовал");
        return;
    }
    advertise(hostname);
    g_started = true;

    // На C3 mDNS иногда не переживает переподключение к точке доступа —
    // поднимаем заново по событию.
    WiFi.onEvent(
        [hostname](WiFiEvent_t, WiFiEventInfo_t) {
            if (!g_started) return;
            MDNS.end();
            if (MDNS.begin(hostname.c_str())) advertise(hostname);
        },
        ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

}  // namespace lumen
