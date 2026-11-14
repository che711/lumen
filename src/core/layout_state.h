// layout_state.h — раскладка диодов, общая для веб-обработчиков и рендера.
//
// Веб-обработчики (ядро 1) пишут, рендер (ядро 0) читает. Отдельный мьютекс и
// счётчик версии нужны, чтобы рендер не копировал раскладку каждый кадр: он
// сравнивает версию и забирает точки только когда они поменялись.
#pragma once

#include <Arduino.h>

#include <vector>

#include "layout.h"

namespace lumen {

// Заменить раскладку целиком. Пустой список — раскладки нет.
void layoutSet(std::vector<layout::Point> pts);

std::vector<layout::Point> layoutGet();

// Растёт при каждом layoutSet. Читается без блокировки.
uint32_t layoutVersion();

// Для рендера: если версия изменилась, копирует точки в dst и обновляет
// version. Возвращает true, когда копия обновилась.
bool layoutSyncTo(std::vector<layout::Point>& dst, uint32_t& version);

}  // namespace lumen
