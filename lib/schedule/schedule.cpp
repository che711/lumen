#include "schedule.h"

#include <algorithm>

namespace lumen {
namespace {

// Разворачиваем якорь в минуты от полуночи дня начала окна.
// Возвращает false, если якорь солнечный, а солнечные времена неизвестны.
bool anchorMinutes(const Anchor& a, const SolarTimes& solar, int& out) {
    switch (a.type) {
        case AnchorType::Clock:
            out = a.value;
            return true;
        case AnchorType::Sunrise:
            if (!solar.valid) return false;
            out = solar.sunriseMin + a.value;
            return true;
        case AnchorType::Sunset:
            if (!solar.valid) return false;
            out = solar.sunsetMin + a.value;
            return true;
    }
    return false;
}

bool isExcepted(const Rule& r, const Date& startDay) {
    for (const Date& d : r.except) {
        if (d == startDay) return true;
    }
    return false;
}

// Огибающая яркости внутри окна.
float envelopeAt(int intoSec, int lengthSec, int fadeInSec, int fadeOutSec) {
    if (lengthSec <= 0) return 1.0f;

    // Если фейды не помещаются в окно — ужимаем их пропорционально.
    if (fadeInSec + fadeOutSec > lengthSec) {
        const float total = static_cast<float>(fadeInSec + fadeOutSec);
        const float scale = static_cast<float>(lengthSec) / total;
        fadeInSec  = static_cast<int>(fadeInSec * scale);
        fadeOutSec = lengthSec - fadeInSec;
    }

    float env = 1.0f;

    if (fadeInSec > 0 && intoSec < fadeInSec) {
        env = static_cast<float>(intoSec) / static_cast<float>(fadeInSec);
    }

    const int leftSec = lengthSec - intoSec;
    if (fadeOutSec > 0 && leftSec < fadeOutSec) {
        const float outEnv =
            static_cast<float>(leftSec) / static_cast<float>(fadeOutSec);
        env = std::min(env, outEnv);
    }

    if (env < 0.0f) env = 0.0f;
    if (env > 1.0f) env = 1.0f;
    return env;
}

struct Candidate {
    const Rule* rule = nullptr;
    int intoSec      = 0;
    int lengthSec    = 0;
};

// Проверяем, попадает ли `now` в окно правила, начавшееся `dayOffset` суток
// назад (0 = сегодня, -1 = вчера).
bool matchWindow(const Rule& r, const Now& now, const SolarTimes& solar,
                 int dayOffset, Candidate& out) {
    const Date startDay = addDays(now.date, dayOffset);

    const int wd = weekdayOf(startDay);
    if (!(r.days & (1 << wd))) return false;
    if (isExcepted(r, startDay)) return false;

    int start = 0, end = 0;
    if (!anchorMinutes(r.from, solar, start)) return false;
    if (!anchorMinutes(r.to, solar, end)) return false;

    // Окно, переходящее через полночь: 22:00 → 06:00.
    if (end <= start) end += 1440;

    // Текущее время в системе координат дня начала окна.
    const int nowMin = now.minuteOfDay - dayOffset * 1440;

    if (nowMin < start || nowMin >= end) return false;

    out.rule      = &r;
    out.intoSec   = (nowMin - start) * 60 + now.second;
    out.lengthSec = (end - start) * 60;
    return true;
}

}  // namespace

Resolved resolve(const Now& now,
                 const SolarTimes& solarToday,
                 const SolarTimes& solarYesterday,
                 const std::vector<Rule>& rules) {
    Candidate best;

    for (const Rule& r : rules) {
        if (!r.enabled) continue;

        // Окно могло начаться сегодня или вчера (если оно переходит полночь).
        for (int dayOffset : {0, -1}) {
            const SolarTimes& solar =
                (dayOffset == 0) ? solarToday : solarYesterday;

            Candidate c;
            if (!matchWindow(r, now, solar, dayOffset, c)) continue;

            if (best.rule == nullptr ||
                c.rule->priority > best.rule->priority ||
                (c.rule->priority == best.rule->priority &&
                 c.rule->id > best.rule->id)) {
                best = c;
            }
            // Одно правило не может совпасть дважды осмысленно — берём первое
            // подходящее окно и переходим к следующему правилу.
            break;
        }
    }

    Resolved res;
    if (best.rule == nullptr) return res;

    res.active            = true;
    res.ruleId            = best.rule->id;
    res.action            = best.rule->action;
    res.secondsIntoWindow = best.intoSec;
    res.windowLengthSec   = best.lengthSec;
    res.envelope          = envelopeAt(best.intoSec, best.lengthSec,
                                       best.rule->fadeInSec,
                                       best.rule->fadeOutSec);
    return res;
}

Resolved resolve(const Now& now,
                 const SolarTimes& solar,
                 const std::vector<Rule>& rules) {
    return resolve(now, solar, solar, rules);
}

}  // namespace lumen
