#include "layout_state.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace lumen {
namespace {

std::vector<layout::Point> g_points;
volatile uint32_t          g_version = 1;   // 0 занят: рендер стартует с нуля
SemaphoreHandle_t          g_mutex = nullptr;

// Мьютекс создаётся при первом обращении. Первое обращение — загрузка
// layout.json в storeBegin(), то есть из setup(), пока других тасков нет.
struct Lock {
    Lock() {
        if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }
    ~Lock() { xSemaphoreGive(g_mutex); }
};

}  // namespace

void layoutSet(std::vector<layout::Point> pts) {
    Lock lock;
    g_points.swap(pts);
    g_version = g_version + 1;
}

std::vector<layout::Point> layoutGet() {
    Lock lock;
    return g_points;
}

uint32_t layoutVersion() { return g_version; }

bool layoutSyncTo(std::vector<layout::Point>& dst, uint32_t& version) {
    Lock lock;
    if (g_version == version) return false;
    dst = g_points;
    version = g_version;
    return true;
}

}  // namespace lumen
