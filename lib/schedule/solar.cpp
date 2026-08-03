#include "solar.h"

#include <cmath>

namespace lumen {
namespace {

constexpr double kPi = 3.14159265358979323846;

inline double rad(double deg) { return deg * kPi / 180.0; }
inline double deg(double r) { return r * 180.0 / kPi; }

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int daysInMonth(int y, int m) {
    static const int table[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && isLeap(y)) return 29;
    return table[m - 1];
}

}  // namespace

int dayOfYear(const Date& d) {
    int n = d.day;
    for (int m = 1; m < d.month; ++m) n += daysInMonth(d.year, m);
    return n;
}

int weekdayOf(const Date& d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = d.year;
    if (d.month < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[d.month - 1] + d.day) % 7;
}

Date addDays(const Date& d, int delta) {
    Date r = d;
    while (delta > 0) {
        int dim = daysInMonth(r.year, r.month);
        if (r.day < dim) {
            r.day++;
        } else {
            r.day = 1;
            if (r.month == 12) { r.month = 1; r.year++; }
            else { r.month++; }
        }
        delta--;
    }
    while (delta < 0) {
        if (r.day > 1) {
            r.day--;
        } else {
            if (r.month == 1) { r.month = 12; r.year--; }
            else { r.month--; }
            r.day = daysInMonth(r.year, r.month);
        }
        delta++;
    }
    return r;
}

SolarTimes computeSolar(const Date& date, double latitude, double longitude,
                        int tzOffsetMin) {
    SolarTimes out;

    const int n = dayOfYear(date);
    const int daysInYear = isLeap(date.year) ? 366 : 365;

    // Дробный угол года (радианы), полдень принимаем за опорную точку.
    const double g = 2.0 * kPi / daysInYear * (n - 1 + (12.0 - 12.0) / 24.0);

    // Уравнение времени, минуты.
    const double eqTime = 229.18 * (0.000075
                                    + 0.001868 * std::cos(g)
                                    - 0.032077 * std::sin(g)
                                    - 0.014615 * std::cos(2 * g)
                                    - 0.040849 * std::sin(2 * g));

    // Склонение Солнца, радианы.
    const double decl = 0.006918
                        - 0.399912 * std::cos(g)
                        + 0.070257 * std::sin(g)
                        - 0.006758 * std::cos(2 * g)
                        + 0.000907 * std::sin(2 * g)
                        - 0.002697 * std::cos(3 * g)
                        + 0.001480 * std::sin(3 * g);

    // Зенитный угол 90.833° учитывает рефракцию и видимый радиус диска.
    const double cosH = std::cos(rad(90.833)) /
                            (std::cos(rad(latitude)) * std::cos(decl))
                        - std::tan(rad(latitude)) * std::tan(decl);

    if (cosH > 1.0 || cosH < -1.0) {
        // Полярная ночь (Солнце не встаёт) или полярный день.
        out.valid = false;
        return out;
    }

    const double haDeg = deg(std::acos(cosH));

    const double sunriseUtc = 720.0 - 4.0 * (longitude + haDeg) - eqTime;
    const double sunsetUtc  = 720.0 - 4.0 * (longitude - haDeg) - eqTime;

    auto toLocal = [&](double utcMin) {
        int m = static_cast<int>(std::lround(utcMin)) + tzOffsetMin;
        while (m < 0) m += 1440;
        while (m >= 1440) m -= 1440;
        return m;
    };

    out.sunriseMin = toLocal(sunriseUtc);
    out.sunsetMin  = toLocal(sunsetUtc);
    out.valid      = true;
    return out;
}

}  // namespace lumen
