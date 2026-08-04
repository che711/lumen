// store.h — хранение конфигурации и расписания на LittleFS.
//
// Всё лежит человекочитаемым JSON: конфиг можно вытащить, отредактировать
// и залить обратно, не собирая прошивку.
#pragma once

#include <Arduino.h>

#include <vector>

#include "config.h"
#include "schedule.h"

namespace lumen {

struct DeviceConfig {
    String  name = "Lumen";

    String  wifiSsid;
    String  wifiPass;

    uint16_t ledCount = 60;
    uint8_t  ledPin   = LED_PIN;

    // Нужны для солнечных якорей расписания.
    double  latitude  = 52.2297;   // Варшава по умолчанию
    double  longitude = 21.0122;
    String  timezone  = "CET-1CEST,M3.5.0,M10.5.0/3";  // POSIX TZ, Польша

    uint8_t bootBrightness = 128;
};

bool storeBegin();

DeviceConfig& config();
bool loadConfig();
bool saveConfig();
void requestConfigSave();          // с дебаунсом, вызывать из обработчиков
void storeTick(uint32_t nowMs);    // выполняет отложенные записи

// Состояние ленты: яркость, питание, сегменты. Хранится отдельно от конфига
// устройства, потому что меняется на каждое движение слайдера.
// Огибающая планировщика не сохраняется — она производная, её пересчитает
// резолвер на первом же тике после загрузки.
bool loadState();
bool saveState();
void requestStateSave();           // с дебаунсом, вызывать из обработчиков

// Сериализация состояния — как и у расписания, используется и хранилищем,
// и эндпоинтом бэкапа, поэтому живёт здесь.
String stateStoreToJson();
bool stateStoreFromJson(const String& json, String& errorOut);

std::vector<Rule>& scheduleRules();
bool loadSchedule();
bool saveSchedule();

// Сериализация расписания в JSON и обратно — используется и хранилищем,
// и HTTP API, поэтому вынесена сюда.
String scheduleToJson();
bool scheduleFromJson(const String& json, String& errorOut);

}  // namespace lumen
