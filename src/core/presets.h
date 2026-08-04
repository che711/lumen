// presets.h — именованные сцены.
//
// Пресеты живут только на LittleFS и читаются по требованию: держать их
// все в RAM незачем, а на C3 каждый килобайт кучи на счету.
//
// Формат presets.json совпадает с WLED настолько, чтобы его понимали
// сторонние инструменты:
//   { "1": { "n": "Вечер", "on": true, "bri": 180, "seg": [ ... ] } }
#pragma once

#include <Arduino.h>

namespace lumen {

// Применить пресет к текущему состоянию. false — пресета нет.
bool presetApply(int id);

// Сохранить текущее состояние как пресет. Пустое имя — сгенерируется.
bool presetSave(int id, const String& name);

bool presetDelete(int id);

// Полный presets.json (для API и бэкапа).
String presetsJson();

// Заменить весь набор пресетов целиком (восстановление из бэкапа).
bool presetsReplace(const String& json, String& errorOut);

// Список id и имён без тяжёлого содержимого — для выпадающих списков.
String presetsIndexJson();

}  // namespace lumen
