// layout.h — раскладка диодов на плоскости.
//
// Лента остаётся одной цепочкой, но у каждого диода есть точка x, y. Эффекты
// по-прежнему считают положение как «номер диода», и менять их не нужно:
// вместо этого кадр рисуется в «виртуальном» порядке, где диоды отсортированы
// по выбранной оси, а потом раскладывается на физические диоды по таблице
// перестановки. Так же уже работает reverse.
//
// Модуль не зависит от Arduino: собирается g++ и тестируется на хосте.
// Разбор layout.json и хранение на LittleFS живут в src/, не здесь.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lumen {
namespace layout {

// Координаты — целые 0..1000, масштаб одинаковый по обеим осям.
constexpr uint16_t kCoordMax = 1000;

struct Point {
    uint16_t x = 0;
    uint16_t y = 0;
};

// По какой оси эффект «видит» ленту. Значения уходят в сохранённые пресеты
// как строки (см. axisName), числа наружу не отдаются.
enum class Axis : uint8_t {
    Wire = 0,  // по порядку провода, как без раскладки
    X,         // слева направо
    Y,         // сверху вниз
    Angle,     // по кругу вокруг центра сегмента
    Center,    // от центра сегмента наружу
};

const char* axisName(Axis axis);

// false и *out не тронут, если имя не из списка.
bool axisFromName(const char* name, Axis* out);

// Строит перестановку для сегмента [start, stop).
//
//   order[v] — смещение (0..len-1) физического диода, который стоит v-м
//   по выбранной оси. len = stop - start.
//
// pts — раскладка всей ленты, count — число точек в ней. Диоды сегмента,
// для которых точки нет (номер >= count), уходят в конец в порядке провода.
// При равных ключах порядок провода сохраняется. Для Wire, пустой раскладки
// и сегмента короче двух диодов получается тождественная перестановка.
//
// Центр для Angle и Center — середина рамки, в которую вписаны диоды самого
// сегмента, поэтому у каждого сегмента эффект расходится от его центра.
//
// Вызывать при смене раскладки, оси или границ сегмента, а не в каждом кадре.
// order должен вмещать len элементов.
void buildOrder(const Point* pts, uint16_t count, uint16_t start,
                uint16_t stop, Axis axis, uint16_t* order);

// Перестановка одного сегмента с кэшем: пересчитывается только при смене
// раскладки, оси или границ сегмента, а не в каждом кадре.
class OrderCache {
   public:
    // Возвращает true, если для сегмента нужно рисовать через перестановку
    // (ось не Wire, раскладка не пуста, в сегменте хотя бы два диода).
    // version — любой счётчик, который растёт при каждой смене раскладки.
    bool update(const Point* pts, uint16_t count, uint32_t version,
                uint16_t start, uint16_t stop, Axis axis);

    // Таблица для scatter; валидна, пока update возвращает true.
    const uint16_t* data() const { return order_.data(); }

    // Сколько раз таблица пересчитывалась — для тестов.
    uint32_t rebuilds() const { return rebuilds_; }

   private:
    bool      haveKey_ = false;
    bool      active_  = false;
    Axis      axis_    = Axis::Wire;
    uint16_t  start_   = 0;
    uint16_t  stop_    = 0;
    uint16_t  count_   = 0;
    uint32_t  version_ = 0;
    uint32_t  rebuilds_ = 0;
    std::vector<uint16_t> order_;
};

// Кадр из виртуального порядка в физический: dst[order[v]] = src[v].
// src и dst не должны пересекаться.
template <typename T>
void scatter(const T* src, T* dst, const uint16_t* order, uint16_t len) {
    for (uint16_t v = 0; v < len; ++v) dst[order[v]] = src[v];
}

}  // namespace layout
}  // namespace lumen
