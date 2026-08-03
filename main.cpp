#include <Arduino.h>

#include "config.h"
#include "core/scheduler_runner.h"
#include "core/state.h"
#include "led/led_controller.h"
#include "net/net.h"
#include "store/store.h"

namespace {

lumen::LedController g_led;
bool g_online = false;

void renderOnce() {
    static uint32_t lastFrameMs = 0;
    const uint32_t now = millis();
    if (now - lastFrameMs < kFrameIntervalMs) return;
    if (!g_led.canShow()) return;
    lastFrameMs = now;
    g_led.render(lumen::stateSnapshot(), now);
}

#ifdef LUMEN_DUAL_CORE
// На WROOM-32 рендер уезжает на нулевое ядро: тогда HTTP-запрос или
// переподключение Wi-Fi не дёргают анимацию.
void renderTask(void*) {
    for (;;) {
        renderOnce();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
#endif

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\nLumen %s (%s)\n", LUMEN_VERSION, LUMEN_BOARD);

    lumen::storeBegin();

    const uint16_t ledCount = lumen::config().ledCount;
    lumen::stateInit(ledCount);

    {
        lumen::StateLock st;
        st->brightness = lumen::config().bootBrightness;
    }

    if (!g_led.begin(ledCount, lumen::config().ledPin)) {
        Serial.println("Не удалось инициализировать ленту");
    }

    g_online = lumen::netConnectOrProvision();

    if (g_online) {
        lumen::discoveryBegin("lumen");
        lumen::webServerBegin();
        lumen::otaBegin("lumen");
        lumen::schedulerBegin();

#ifdef LUMEN_DUAL_CORE
        xTaskCreatePinnedToCore(renderTask, "render", 4096, nullptr, 2,
                                nullptr, 0);
        Serial.println("Рендер вынесен на ядро 0");
#endif
    } else {
        // В режиме портала лента гасится: устройство ещё не настроено,
        // мигать в этот момент незачем.
        g_led.blackout();
    }
}

void loop() {
    const uint32_t now = millis();

    lumen::netTick();

    if (!g_online) {
        delay(5);
        return;
    }

    lumen::otaTick();
    lumen::schedulerTick(now);
    lumen::storeTick(now);

    if (lumen::stateConsumeDirty()) {
        lumen::webBroadcastState();
    }

#ifndef LUMEN_DUAL_CORE
    // На одноядерном C3 рендер живёт в основном цикле.
    renderOnce();
#endif

    delay(1);
}

