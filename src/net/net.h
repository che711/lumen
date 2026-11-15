// net.h — сетевой слой: подключение, портал настройки, HTTP/WS, OTA.
#pragma once

#include <Arduino.h>

namespace lumen {

// Пытается подключиться к сохранённой сети. Если не вышло — поднимает
// точку доступа с captive-порталом и НЕ возвращает управление
// (устройство перезагрузится после сохранения настроек).
bool netConnectOrProvision();

void netTick();

void webServerBegin();
void webBroadcastState();   // толкнуть текущее состояние в WebSocket
void webTick(uint32_t nowMs);   // кадры предпросмотра подписчикам

void otaBegin(const String& hostname);
void otaTick();

}  // namespace lumen
