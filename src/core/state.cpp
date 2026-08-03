#include "state.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace lumen {
namespace {

AppState           g_state;
SemaphoreHandle_t  g_mutex = nullptr;
volatile bool      g_dirty = false;

}  // namespace

StateLock::StateLock() {
    if (g_mutex) {
        acquired_ = xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE;
    }
}

StateLock::~StateLock() {
    if (acquired_) xSemaphoreGive(g_mutex);
}

AppState* StateLock::operator->() { return &g_state; }
AppState& StateLock::operator*() { return g_state; }

void stateInit(uint16_t ledCount) {
    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();

    StateLock st;
    st->segments[0].active = true;
    st->segments[0].start  = 0;
    st->segments[0].stop   = ledCount;
    st->segments[0].on     = true;
    for (uint8_t i = 1; i < MAX_SEGMENTS; ++i) {
        st->segments[i].active = false;
    }
}

AppState stateSnapshot() {
    StateLock st;
    return *st;
}

void stateTouch() { g_dirty = true; }

bool stateConsumeDirty() {
    if (!g_dirty) return false;
    g_dirty = false;
    return true;
}

}  // namespace lumen
