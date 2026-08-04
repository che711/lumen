// schedule.h — декларативный планировщик.
//
// Ключевое отличие от событийной модели WLED: правило описывает ИНТЕРВАЛ,
// а не момент. Резолвер отвечает на вопрос «какое правило действует прямо
// сейчас», а не «какое событие только что произошло».
//
// Практическое следствие: перезагрузка контроллера в середине окна не теряет
// состояние — после старта резолвер сразу вернёт нужное правило и позицию
// внутри его fade-огибающей.
//
// Модуль не зависит от Arduino: собирается g++ и тестируется на хосте.
#pragma once

#include <cstdint>
#include <vector>

#include "solar.h"

namespace lumen {

// ---------------------------------------------------------------- якоря

enum class AnchorType : uint8_t {
    Clock,    // фиксированное время суток
    Sunrise,  // восход + offset
    Sunset,   // закат + offset
};

struct Anchor {
    AnchorType type = AnchorType::Clock;
    // Clock: минуты от полуночи (0..1439).
    // Sunrise/Sunset: смещение в минутах, может быть отрицательным.
    int value = 0;

    static Anchor clock(int hour, int minute) {
        return Anchor{AnchorType::Clock, hour * 60 + minute};
    }
    static Anchor sunset(int offsetMin = 0) {
        return Anchor{AnchorType::Sunset, offsetMin};
    }
    static Anchor sunrise(int offsetMin = 0) {
        return Anchor{AnchorType::Sunrise, offsetMin};
    }
};

// ---------------------------------------------------------------- правило

struct Action {
    int preset     = -1;  // -1 — не менять пресет
    int brightness = -1;  // 0..255, -1 — брать яркость из пресета
    bool powerOff  = false;  // правило гасит ленту вместо применения пресета
};

// Биты дней недели: бит 0 — воскресенье, бит 6 — суббота.
namespace Days {
constexpr uint8_t Sun = 1 << 0;
constexpr uint8_t Mon = 1 << 1;
constexpr uint8_t Tue = 1 << 2;
constexpr uint8_t Wed = 1 << 3;
constexpr uint8_t Thu = 1 << 4;
constexpr uint8_t Fri = 1 << 5;
constexpr uint8_t Sat = 1 << 6;
constexpr uint8_t Weekdays = Mon | Tue | Wed | Thu | Fri;
constexpr uint8_t Weekend  = Sat | Sun;
constexpr uint8_t All      = 0x7F;
}  // namespace Days

struct Rule {
    int      id       = 0;
    bool     enabled  = true;
    int      priority = 0;      // больше — важнее

    Anchor   from;
    Anchor   to;
    uint8_t  days = Days::All;  // применяется ко дню НАЧАЛА окна

    std::vector<Date> except;   // календарные исключения (по дню начала)

    Action   action;
    int      fadeInSec  = 0;
    int      fadeOutSec = 0;
};

// ---------------------------------------------------------------- время

struct Now {
    Date date;
    int  minuteOfDay = 0;  // 0..1439
    int  second      = 0;  // 0..59, нужен только для плавности fade
};

// ---------------------------------------------------------------- результат

struct Resolved {
    bool  active   = false;
    int   ruleId   = -1;
    Action action;

    // Множитель яркости 0.0..1.0 с учётом fadeIn/fadeOut.
    // Вне окна — 0.0; в «полке» между фейдами — 1.0.
    float envelope = 0.0f;

    int secondsIntoWindow = 0;
    int windowLengthSec   = 0;
};

// Основная функция. `solarToday` — солнечные времена для now.date;
// для окна, начавшегося вчера, используется `solarYesterday`.
// Если солнечные времена невалидны, правила с солнечными якорями игнорируются.
Resolved resolve(const Now& now,
                 const SolarTimes& solarToday,
                 const SolarTimes& solarYesterday,
                 const std::vector<Rule>& rules);

// Удобная перегрузка, когда разницей между вчера и сегодня можно пренебречь
// (сдвиг восхода за сутки — единицы минут).
Resolved resolve(const Now& now,
                 const SolarTimes& solar,
                 const std::vector<Rule>& rules);

}  // namespace lumen
