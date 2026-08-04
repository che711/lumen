// solar.h — вычисление времени восхода/заката (NOAA solar calculator).
//
// Модуль намеренно не зависит от Arduino: его можно собрать обычным g++
// и покрыть тестами на хосте. Всё время — в минутах от локальной полуночи.
#pragma once

namespace lumen {

struct Date {
    int year  = 1970;
    int month = 1;   // 1..12
    int day   = 1;   // 1..31

    bool operator==(const Date& o) const {
        return year == o.year && month == o.month && day == o.day;
    }
};

// Времена солнечных событий в минутах от локальной полуночи.
// valid == false означает полярный день/ночь либо неизвестные координаты —
// правила с солнечными якорями в этом случае просто не срабатывают.
struct SolarTimes {
    int  sunriseMin = 0;
    int  sunsetMin  = 0;
    bool valid      = false;
};

// Порядковый номер дня в году (1..366).
int dayOfYear(const Date& d);

// День недели: 0 = воскресенье ... 6 = суббота (алгоритм Sakamoto).
int weekdayOf(const Date& d);

// Прибавить к дате целое число дней (может быть отрицательным).
Date addDays(const Date& d, int delta);

// Восход/закат для даты и координат.
// tzOffsetMin — смещение локального времени от UTC в минутах,
//               уже с учётом летнего времени (для Польши: 60 зимой, 120 летом).
SolarTimes computeSolar(const Date& date, double latitude, double longitude,
                        int tzOffsetMin);

}  // namespace lumen
