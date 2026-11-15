#include <ArduinoOTA.h>

#include "net.h"
#include "store/store.h"

namespace lumen {
namespace {
bool g_ready = false;
}

void otaBegin(const String& hostname) {
    ArduinoOTA.setHostname(hostname.c_str());

    // ArduinoOTA по умолчанию сам поднимает mDNS. Он не нужен: прошивать
    // по воздуху надо по IP.
    ArduinoOTA.setMdnsEnabled(false);

    // Без пароля перепрошить устройство может кто угодно в локальной сети.
    // Молча оставлять так нельзя — если пароля нет, пишем об этом в лог.
    if (!config().apiPassword.isEmpty()) {
        ArduinoOTA.setPassword(config().apiPassword.c_str());
    } else {
        log_w("OTA без пароля: обновить прошивку может любой в этой сети. "
              "Задать — POST /api/config с полем apiPass");
    }

    ArduinoOTA.onStart([]() {
        // Лента во время прошивки должна погаснуть: RMT и запись во флеш
        // соревнуются за шину, картинка всё равно порвётся.
        log_i("OTA: начало обновления");
    });
    ArduinoOTA.onEnd([]() { log_i("OTA: готово, перезапуск"); });
    ArduinoOTA.onError([](ota_error_t error) {
        log_e("OTA: ошибка %u", error);
    });

    ArduinoOTA.begin();
    g_ready = true;
    log_i("OTA доступно по IP");
}

void otaTick() {
    if (g_ready) ArduinoOTA.handle();
}

}  // namespace lumen
