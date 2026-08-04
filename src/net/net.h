// net.h — сетевой слой: подключение, портал настройки, mDNS, HTTP/WS, OTA.
#pragma once

#include <Arduino.h>

namespace lumen {

// Пытается подключиться к сохранённой сети. Если не вышло — поднимает
// точку доступа с captive-порталом и НЕ возвращает управление
// (устройство перезагрузится после сохранения настроек).
bool netConnectOrProvision();

void netTick();

// Анонс mDNS. Сервис `_wled._tcp` — именно его слушает нативная
// интеграция WLED в Home Assistant, поэтому устройство находится
// автоматически, без custom_component и без YAML.
void discoveryBegin(const String& hostname);

void webServerBegin();
void webBroadcastState();   // толкнуть текущее состояние в WebSocket

void otaBegin(const String& hostname);
void otaTick();

}  // namespace lumen
