#include "live_state.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"

namespace lumen {
namespace {

constexpr uint32_t kIdentifyMs = 5000;

uint8_t           g_rgb[3 * MAX_LEDS];
uint16_t          g_count = 0;
bool              g_fresh = false;
volatile bool     g_wanted = false;
volatile int      g_identify = -1;
volatile uint32_t g_identifyUntil = 0;
SemaphoreHandle_t g_mutex = nullptr;

// Мьютекс создаётся при первом обращении, а оно приходит из рендера или
// веб-сервера уже после setup(). Обращения из двух тасков в один момент
// возможны, поэтому создаём под критической секцией.
portMUX_TYPE g_initMux = portMUX_INITIALIZER_UNLOCKED;

struct Lock {
    Lock() {
        if (!g_mutex) {
            SemaphoreHandle_t m = xSemaphoreCreateMutex();
            portENTER_CRITICAL(&g_initMux);
            if (!g_mutex) {
                g_mutex = m;
                m = nullptr;
            }
            portEXIT_CRITICAL(&g_initMux);
            if (m) vSemaphoreDelete(m);
        }
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }
    ~Lock() { xSemaphoreGive(g_mutex); }
};

}  // namespace

void liveSetWanted(bool wanted) { g_wanted = wanted; }
bool liveWanted() { return g_wanted; }

void livePublish(const uint8_t* rgb, uint16_t count) {
    if (count > MAX_LEDS) count = MAX_LEDS;
    Lock lock;
    memcpy(g_rgb, rgb, 3u * count);
    g_count = count;
    g_fresh = true;
}

uint16_t liveRead(uint8_t* dst, uint16_t cap) {
    Lock lock;
    if (!g_fresh) return 0;
    g_fresh = false;
    const uint16_t n = g_count < cap ? g_count : cap;
    memcpy(dst, g_rgb, 3u * n);
    return n;
}

void liveIdentify(int led, uint32_t nowMs) {
    g_identifyUntil = nowMs + kIdentifyMs;
    g_identify = led;
}

int liveIdentified(uint32_t nowMs) {
    const int led = g_identify;
    if (led < 0) return -1;
    // Разность без знака: корректно и при переходе millis() через ноль.
    if (static_cast<int32_t>(nowMs - g_identifyUntil) >= 0) {
        g_identify = -1;
        return -1;
    }
    return led;
}

}  // namespace lumen
