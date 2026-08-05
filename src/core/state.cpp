#include "state.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace lumen {
namespace {

AppState           g_state;
SemaphoreHandle_t  g_mutex = nullptr;
volatile bool      g_dirty = false;

}  // namespace

// Раньше здесь стоял захват с таймаутом 50 мс, а operator-> отдавал доступ к
// состоянию независимо от того, удался он или нет. По таймауту это давало
// молчаливую гонку: рендер на ядре 0 читает AppState, пока веб-обработчик на
// ядре 1 его правит. Теперь ждём столько, сколько нужно, — держат мьютекс
// микросекунды, а единственный способ получить настоящий дедлок разобран
// отдельным случаем ниже.
StateLock::StateLock() {
    if (!g_mutex) return;

    // Повторный захват тем же таском: мьютекс FreeRTOS не рекурсивный, и
    // ожидание здесь означало бы вечный сон. Но раз мы уже внутри критической
    // секции этого же таска, состояние и так защищено — просто не берём
    // мьютекс второй раз и не отдаём его в деструкторе.
    if (xSemaphoreGetMutexHolder(g_mutex) == xTaskGetCurrentTaskHandle()) {
        return;
    }

    acquired_ = xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE;
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
