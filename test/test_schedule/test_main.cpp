// Тесты ядра планировщика. Собираются и на хосте (g++), и через
// `pio test -e native` (test_framework = custom).
//
//   g++ -std=c++17 -I lib/schedule lib/schedule/*.cpp
//       test/test_schedule/test_main.cpp -o /tmp/lumen_test && /tmp/lumen_test

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "schedule.h"
#include "solar.h"

using namespace lumen;

// ------------------------------------------------------- крошечный харнесс

static int g_failed = 0;
static int g_total  = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        g_total++;                                                    \
        if (!(cond)) {                                                \
            g_failed++;                                               \
            std::printf("  FAIL  %s\n        (%s:%d)\n", msg,         \
                        __FILE__, __LINE__);                          \
        }                                                             \
    } while (0)

#define SECTION(name) std::printf("\n== %s\n", name)

static bool near(int a, int b, int tol) { return std::abs(a - b) <= tol; }

// ------------------------------------------------------- вспомогательное

static Now at(int y, int m, int d, int hh, int mm, int ss = 0) {
    Now n;
    n.date        = Date{y, m, d};
    n.minuteOfDay = hh * 60 + mm;
    n.second      = ss;
    return n;
}

static SolarTimes fixedSolar(int sunriseMin, int sunsetMin) {
    SolarTimes s;
    s.sunriseMin = sunriseMin;
    s.sunsetMin  = sunsetMin;
    s.valid      = true;
    return s;
}

// ------------------------------------------------------- календарь

static void testCalendar() {
    SECTION("Календарь");

    // 2 августа 2026 — воскресенье.
    CHECK((weekdayOf(Date{2026, 8, 2}) == 0), "2026-08-02 это воскресенье");
    // 1 января 2026 — четверг.
    CHECK((weekdayOf(Date{2026, 1, 1}) == 4), "2026-01-01 это четверг");

    CHECK((dayOfYear(Date{2026, 1, 1}) == 1), "1 января — день 1");
    CHECK((dayOfYear(Date{2026, 12, 31}) == 365), "31 декабря 2026 — день 365");
    CHECK((dayOfYear(Date{2024, 12, 31}) == 366), "2024 високосный — день 366");

    CHECK((addDays(Date{2026, 1, 1}, -1) == Date{2025, 12, 31}),
          "переход через год назад");
    CHECK((addDays(Date{2026, 2, 28}, 1) == Date{2026, 3, 1}),
          "невисокосный февраль");
    CHECK((addDays(Date{2024, 2, 28}, 1) == Date{2024, 2, 29}),
          "високосный февраль");
}

// ------------------------------------------------------- солнце

static void testSolar() {
    SECTION("Восход и закат (Варшава)");

    const double lat = 52.2297, lon = 21.0122;

    // Летнее солнцестояние, CEST (UTC+2).
    SolarTimes june = computeSolar(Date{2026, 6, 21}, lat, lon, 120);
    CHECK(june.valid, "21 июня: результат валиден");
    CHECK(near(june.sunriseMin, 4 * 60 + 14, 10), "21 июня: восход ~04:14");
    CHECK(near(june.sunsetMin, 21 * 60 + 1, 10), "21 июня: закат ~21:01");

    // Зимнее солнцестояние, CET (UTC+1).
    SolarTimes dec = computeSolar(Date{2026, 12, 21}, lat, lon, 60);
    CHECK(dec.valid, "21 декабря: результат валиден");
    CHECK(near(dec.sunriseMin, 7 * 60 + 44, 10), "21 декабря: восход ~07:44");
    CHECK(near(dec.sunsetMin, 15 * 60 + 24, 10), "21 декабря: закат ~15:24");

    // Полярная ночь: Тромсё в декабре — Солнце не встаёт.
    SolarTimes polar = computeSolar(Date{2026, 12, 21}, 69.65, 18.96, 60);
    CHECK(!polar.valid, "полярная ночь помечается как невалидная");
}

// ------------------------------------------------------- базовые окна

static void testBasicWindow() {
    SECTION("Простое окно");

    Rule r;
    r.id     = 1;
    r.from   = Anchor::clock(18, 0);
    r.to     = Anchor::clock(23, 0);
    r.days   = Days::All;
    r.action = Action{3, 200, false};

    std::vector<Rule> rules{r};
    SolarTimes solar = fixedSolar(300, 1200);

    CHECK(!resolve(at(2026, 8, 2, 17, 59), solar, rules).active,
          "до окна — не активно");
    CHECK(resolve(at(2026, 8, 2, 18, 0), solar, rules).active,
          "в момент старта — активно");
    CHECK(resolve(at(2026, 8, 2, 22, 59), solar, rules).active,
          "за минуту до конца — активно");
    CHECK(!resolve(at(2026, 8, 2, 23, 0), solar, rules).active,
          "конец окна не включается");

    Resolved mid = resolve(at(2026, 8, 2, 20, 0), solar, rules);
    CHECK(mid.ruleId == 1, "вернулся правильный id");
    CHECK(mid.action.preset == 3, "вернулся пресет правила");
    CHECK(std::fabs(mid.envelope - 1.0f) < 0.001f,
          "без фейдов огибающая равна 1.0");
}

// ------------------------------------------------------- дни недели

static void testWeekdays() {
    SECTION("Дни недели и исключения");

    Rule r;
    r.id   = 1;
    r.from = Anchor::clock(9, 0);
    r.to   = Anchor::clock(17, 0);
    r.days = Days::Weekdays;

    std::vector<Rule> rules{r};
    SolarTimes solar = fixedSolar(300, 1200);

    // 3 августа 2026 — понедельник, 2 августа — воскресенье.
    CHECK(resolve(at(2026, 8, 3, 12, 0), solar, rules).active,
          "понедельник входит в будни");
    CHECK(!resolve(at(2026, 8, 2, 12, 0), solar, rules).active,
          "воскресенье не входит в будни");

    rules[0].except.push_back(Date{2026, 8, 3});
    CHECK(!resolve(at(2026, 8, 3, 12, 0), solar, rules).active,
          "календарное исключение отменяет правило");
    CHECK(resolve(at(2026, 8, 4, 12, 0), solar, rules).active,
          "исключение действует только на свой день");
}

// ------------------------------------------------------- через полночь

static void testMidnightWrap() {
    SECTION("Окно через полночь");

    Rule r;
    r.id   = 1;
    r.from = Anchor::clock(22, 0);
    r.to   = Anchor::clock(6, 0);
    r.days = Days::Sat;  // окно СТАРТУЕТ в субботу

    std::vector<Rule> rules{r};
    SolarTimes solar = fixedSolar(300, 1200);

    // 1 августа 2026 — суббота, 2 августа — воскресенье.
    CHECK(resolve(at(2026, 8, 1, 23, 30), solar, rules).active,
          "субботний вечер внутри окна");
    CHECK(resolve(at(2026, 8, 2, 3, 0), solar, rules).active,
          "ночь воскресенья всё ещё в субботнем окне");
    CHECK(!resolve(at(2026, 8, 2, 6, 0), solar, rules).active,
          "в 06:00 окно закрылось");
    CHECK(!resolve(at(2026, 8, 2, 23, 30), solar, rules).active,
          "вечер воскресенья окно не открывает");
}

// ------------------------------------------------------- перезагрузка

static void testRebootRecovery() {
    SECTION("Восстановление после перезагрузки");

    // Именно этот сценарий ломается в событийной модели: контроллер
    // перезагрузился в 19:00, событие «18:00 включить» уже прошло.
    Rule r;
    r.id     = 1;
    r.from   = Anchor::clock(18, 0);
    r.to     = Anchor::clock(23, 0);
    r.action = Action{3, 200, false};

    std::vector<Rule> rules{r};
    SolarTimes solar = fixedSolar(300, 1200);

    Resolved afterBoot = resolve(at(2026, 8, 2, 19, 0), solar, rules);
    CHECK(afterBoot.active, "после ребута в середине окна состояние найдено");
    CHECK(afterBoot.secondsIntoWindow == 3600,
          "позиция внутри окна вычислена верно");
}

// ------------------------------------------------------- фейды

static void testEnvelope() {
    SECTION("Огибающая fade in/out");

    Rule r;
    r.id         = 1;
    r.from       = Anchor::clock(18, 0);
    r.to         = Anchor::clock(23, 0);
    r.fadeInSec  = 1800;  // 30 минут
    r.fadeOutSec = 600;   // 10 минут

    std::vector<Rule> rules{r};
    SolarTimes solar = fixedSolar(300, 1200);

    Resolved start = resolve(at(2026, 8, 2, 18, 0), solar, rules);
    CHECK(std::fabs(start.envelope) < 0.01f, "в начале фейда огибающая ~0");

    Resolved quarter = resolve(at(2026, 8, 2, 18, 15), solar, rules);
    CHECK(std::fabs(quarter.envelope - 0.5f) < 0.01f,
          "на середине fade-in огибающая ~0.5");

    Resolved plateau = resolve(at(2026, 8, 2, 20, 0), solar, rules);
    CHECK(std::fabs(plateau.envelope - 1.0f) < 0.01f, "на полке огибающая 1.0");

    Resolved fading = resolve(at(2026, 8, 2, 22, 55), solar, rules);
    CHECK(std::fabs(fading.envelope - 0.5f) < 0.01f,
          "на середине fade-out огибающая ~0.5");

    // Фейды длиннее окна должны ужиматься, а не ломать логику.
    rules[0].from       = Anchor::clock(18, 0);
    rules[0].to         = Anchor::clock(18, 10);
    rules[0].fadeInSec  = 3600;
    rules[0].fadeOutSec = 3600;
    Resolved squeezed = resolve(at(2026, 8, 2, 18, 5), solar, rules);
    CHECK(squeezed.active, "короткое окно с длинными фейдами активно");
    CHECK(squeezed.envelope > 0.9f && squeezed.envelope <= 1.0f,
          "фейды ужаты пропорционально, пик в середине");
}

// ------------------------------------------------------- приоритеты

static void testPriority() {
    SECTION("Приоритеты правил");

    Rule base;
    base.id       = 1;
    base.priority = 0;
    base.from     = Anchor::clock(0, 0);
    base.to       = Anchor::clock(23, 59);
    base.action   = Action{1, 100, false};

    Rule party;
    party.id       = 2;
    party.priority = 10;
    party.from     = Anchor::clock(20, 0);
    party.to       = Anchor::clock(22, 0);
    party.action   = Action{7, 255, false};

    std::vector<Rule> rules{base, party};
    SolarTimes solar = fixedSolar(300, 1200);

    CHECK(resolve(at(2026, 8, 2, 12, 0), solar, rules).ruleId == 1,
          "днём действует фоновое правило");
    CHECK(resolve(at(2026, 8, 2, 21, 0), solar, rules).ruleId == 2,
          "приоритетное правило перебивает фоновое");
    CHECK(resolve(at(2026, 8, 2, 21, 0), solar, rules).action.preset == 7,
          "вернулось действие приоритетного правила");

    rules[1].enabled = false;
    CHECK(resolve(at(2026, 8, 2, 21, 0), solar, rules).ruleId == 1,
          "выключенное правило игнорируется");
}

// ------------------------------------------------------- солнечные якоря

static void testSolarAnchors() {
    SECTION("Солнечные якоря");

    Rule r;
    r.id   = 1;
    r.from = Anchor::sunset(-30);       // за полчаса до заката
    r.to   = Anchor::clock(23, 30);

    std::vector<Rule> rules{r};
    SolarTimes solar = fixedSolar(4 * 60 + 14, 21 * 60 + 1);  // закат 21:01

    CHECK(!resolve(at(2026, 6, 21, 20, 25), solar, rules).active,
          "за 36 минут до заката ещё рано");
    CHECK(resolve(at(2026, 6, 21, 20, 35), solar, rules).active,
          "за 26 минут до заката уже активно");

    SolarTimes invalid;  // valid == false
    CHECK(!resolve(at(2026, 6, 21, 20, 35), invalid, rules).active,
          "без солнечных данных солнечное правило не срабатывает");

    // А правило на часах в тех же условиях работать обязано.
    Rule clockRule;
    clockRule.id   = 2;
    clockRule.from = Anchor::clock(20, 0);
    clockRule.to   = Anchor::clock(23, 0);
    std::vector<Rule> mixed{r, clockRule};
    CHECK(resolve(at(2026, 6, 21, 20, 35), invalid, mixed).ruleId == 2,
          "правило на часах работает без солнечных данных");
}

// ------------------------------------------------------- прогон года

static void testYearSweep() {
    SECTION("Прогон года без падений");

    Rule evening;
    evening.id         = 1;
    evening.from       = Anchor::sunset(-15);
    evening.to         = Anchor::clock(23, 0);
    evening.fadeInSec  = 900;
    evening.fadeOutSec = 900;

    Rule night;
    night.id       = 2;
    night.priority = 5;
    night.from     = Anchor::clock(23, 0);
    night.to       = Anchor::sunrise(0);
    night.action   = Action{0, 20, false};

    std::vector<Rule> rules{evening, night};

    int activeMinutes = 0;
    bool envelopeSane = true;

    Date d{2026, 1, 1};
    for (int day = 0; day < 365; ++day) {
        const int tz = (d.month >= 4 && d.month <= 9) ? 120 : 60;
        SolarTimes today = computeSolar(d, 52.2297, 21.0122, tz);
        SolarTimes yday  = computeSolar(addDays(d, -1), 52.2297, 21.0122, tz);

        for (int minute = 0; minute < 1440; minute += 5) {
            Now n;
            n.date        = d;
            n.minuteOfDay = minute;
            Resolved res  = resolve(n, today, yday, rules);
            if (res.active) {
                activeMinutes += 5;
                if (res.envelope < 0.0f || res.envelope > 1.0f) {
                    envelopeSane = false;
                }
            }
        }
        d = addDays(d, 1);
    }

    CHECK(envelopeSane, "огибающая всегда в диапазоне 0..1");
    CHECK(activeMinutes > 0, "за год правила хоть раз сработали");
    std::printf("  (активных минут за год: %d)\n", activeMinutes);
}

// ------------------------------------------------------- main

int main() {
    std::printf("Lumen — тесты ядра планировщика");

    testCalendar();
    testSolar();
    testBasicWindow();
    testWeekdays();
    testMidnightWrap();
    testRebootRecovery();
    testEnvelope();
    testPriority();
    testSolarAnchors();
    testYearSweep();

    std::printf("\n----------------------------------------\n");
    std::printf("Пройдено %d из %d\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
