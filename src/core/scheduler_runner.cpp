#include "scheduler_runner.h"

#include <ArduinoJson.h>
#include <time.h>

#include "config.h"
#include "net/net.h"
#include "presets.h"
#include "schedule.h"
#include "solar.h"
#include "state.h"
#include "store/store.h"

namespace lumen {
namespace {

uint32_t   g_lastTickMs = 0;
int        g_lastRuleId = -2;   // -2 — «ещё ни разу не считали»
SolarTimes g_solarToday;
int        g_tzOffsetMin = 0;

// Смещение локального времени от UTC в минутах, с учётом летнего времени.
// Считаем сравнением полей, а не через tm_gmtoff: последнего может не быть
// в конкретной сборке newlib.
int tzOffsetMinutes(time_t t) {
    struct tm lt {};
    struct tm gt {};
    localtime_r(&t, &lt);
    gmtime_r(&t, &gt);

    int minutes = (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);

    // Коррекция, если локальная дата ушла на сутки вперёд или назад.
    const int dayDiff = lt.tm_yday - gt.tm_yday;
    if (dayDiff == 1 || dayDiff < -1) minutes += 1440;
    else if (dayDiff == -1 || dayDiff > 1) minutes -= 1440;

    return minutes;
}

void applyResolved(const Resolved& res, bool ruleChanged) {
    if (!res.active) {
        StateLock st;
        // Правил нет — планировщик отпускает управление: ручные изменения
        // и команды из Home Assistant продолжают работать как обычно.
        st->scheduleEnvelope = 1.0f;
        return;
    }

    // Пресет применяем только в момент смены правила: иначе он затирал бы
    // ручные правки раз в секунду, и покрутить цвет было бы невозможно.
    // presetApply берёт мьютекс состояния сам, поэтому — до StateLock.
    if (ruleChanged && res.action.preset > 0) {
        presetApply(res.action.preset);
    }

    StateLock st;
    st->scheduleEnvelope = res.envelope;

    if (res.action.powerOff) {
        st->on = false;
    } else {
        st->on = true;
        if (res.action.brightness >= 0) {
            st->brightness = static_cast<uint8_t>(res.action.brightness);
        }
    }
}

}  // namespace

void schedulerBegin() {
    // POSIX TZ строкой — переход на летнее время устройство считает само,
    // без обращения к сети и без таблицы правил в прошивке.
    configTzTime(config().timezone.c_str(), "pool.ntp.org", "time.google.com");
    log_i("NTP запрошен, TZ=%s", config().timezone.c_str());
}

bool timeIsValid() {
    time_t now = time(nullptr);
    // Всё, что раньше 2023 года, — это ещё не синхронизированные часы.
    return now > 1672531200;
}

String schedulerStatusJson() {
    JsonDocument doc;
    doc["timeValid"] = timeIsValid();
    doc["tzOffset"]  = g_tzOffsetMin;
    doc["activeRule"] = g_lastRuleId < 0 ? -1 : g_lastRuleId;
    doc["rules"]      = scheduleRules().size();

    if (timeIsValid()) {
        const time_t t = time(nullptr);
        struct tm lt {};
        localtime_r(&t, &lt);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        doc["now"] = buf;
        doc["minuteOfDay"] = lt.tm_hour * 60 + lt.tm_min;
        doc["weekday"] = lt.tm_wday;
    }

    JsonObject sun = doc["sun"].to<JsonObject>();
    sun["valid"] = g_solarToday.valid;
    sun["sunrise"] = g_solarToday.sunriseMin;
    sun["sunset"]  = g_solarToday.sunsetMin;

    String out;
    serializeJson(doc, out);
    return out;
}

void schedulerTick(uint32_t nowMs) {
    if (nowMs - g_lastTickMs < kScheduleTickMs) return;
    g_lastTickMs = nowMs;

    if (!timeIsValid()) return;
    if (scheduleRules().empty()) return;

    const time_t t = time(nullptr);
    struct tm lt {};
    localtime_r(&t, &lt);

    Now now;
    now.date        = Date{lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday};
    now.minuteOfDay = lt.tm_hour * 60 + lt.tm_min;
    now.second      = lt.tm_sec;

    const int tzMin = tzOffsetMinutes(t);
    g_tzOffsetMin = tzMin;

    const SolarTimes today =
        computeSolar(now.date, config().latitude, config().longitude, tzMin);
    const SolarTimes yesterday = computeSolar(
        addDays(now.date, -1), config().latitude, config().longitude, tzMin);
    g_solarToday = today;

    const Resolved res = resolve(now, today, yesterday, scheduleRules());

    const bool ruleChanged = (res.ruleId != g_lastRuleId);
    applyResolved(res, ruleChanged);

    if (ruleChanged) {
        g_lastRuleId = res.ruleId;
        if (res.active) {
            log_i("Расписание: активно правило %d (огибающая %.2f)",
                  res.ruleId, res.envelope);
        } else {
            log_i("Расписание: активных правил нет");
        }
        webBroadcastState();
    }
}

}  // namespace lumen
