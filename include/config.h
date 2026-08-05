#pragma once

#include <Arduino.h>

// Значения по умолчанию; всё, что имеет смысл менять на лету,
// живёт в config.json на LittleFS и переопределяет эти константы.

#ifndef LED_PIN
#define LED_PIN 16
#endif

#ifndef MAX_LEDS
#define MAX_LEDS 300
#endif

#ifndef MAX_SEGMENTS
#define MAX_SEGMENTS 4
#endif

#ifndef LUMEN_VERSION
#define LUMEN_VERSION "0.1.0"
#endif

#ifndef WLED_COMPAT_VERSION
#define WLED_COMPAT_VERSION "0.15.0"
#endif

#ifndef LUMEN_BOARD
#define LUMEN_BOARD "unknown"
#endif

// Целевой FPS рендера. 42 — компромисс: плавно для глаза и оставляет
// C3 достаточно времени на обслуживание Wi-Fi между кадрами.
constexpr uint16_t kTargetFps = 42;
constexpr uint16_t kFrameIntervalMs = 1000 / kTargetFps;

// Планировщик пересчитывается раз в секунду — этого хватает для плавных
// многоминутных фейдов и почти ничего не стоит.
constexpr uint32_t kScheduleTickMs = 1000;

// Дебаунс записи на LittleFS: не пишем конфиг на каждое движение слайдера.
constexpr uint32_t kConfigWriteDebounceMs = 8000;

// Портал настройки поднимается, если за это время не удалось подключиться.
constexpr uint32_t kWifiConnectTimeoutMs = 20000;

constexpr const char* kConfigPath   = "/config.json";
constexpr const char* kSchedulePath = "/schedule.json";
constexpr const char* kPresetsPath  = "/presets.json";
// Состояние ленты — отдельно от конфига: меняется в разы чаще и не должно
// тащить за собой перезапись настроек устройства.
constexpr const char* kStatePath    = "/state.json";
