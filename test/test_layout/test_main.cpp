// Тесты раскладки диодов. Собираются на хосте:
//
//   g++ -std=c++17 -Wall -Wextra -I lib/layout lib/layout/*.cpp
//       test/test_layout/test_main.cpp -o /tmp/lumen_layout && /tmp/lumen_layout

#include <cmath>
#include <cstdio>
#include <vector>

#include "layout.h"

using namespace lumen::layout;

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

// ------------------------------------------------------- вспомогательное

using Order = std::vector<uint16_t>;

static Order build(const std::vector<Point>& pts, uint16_t start,
                   uint16_t stop, Axis axis) {
    Order o(stop > start ? stop - start : 0);
    buildOrder(pts.data(), static_cast<uint16_t>(pts.size()), start, stop,
               axis, o.data());
    return o;
}

static bool isPermutation(const Order& o) {
    std::vector<bool> seen(o.size(), false);
    for (uint16_t v : o) {
        if (v >= o.size() || seen[v]) return false;
        seen[v] = true;
    }
    return true;
}

static std::vector<Point> make(std::initializer_list<Point> l) { return l; }

// Детерминированный генератор — тесты не должны зависеть от rand().
static uint32_t g_seed = 12345;
static uint32_t next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

static std::vector<Point> randomPoints(size_t n) {
    std::vector<Point> p(n);
    for (auto& q : p) {
        q.x = static_cast<uint16_t>(next() % (kCoordMax + 1));
        q.y = static_cast<uint16_t>(next() % (kCoordMax + 1));
    }
    return p;
}

// ------------------------------------------------------- тождественные случаи

static void testIdentity() {
    SECTION("тождественная перестановка");
    const auto pts = make({{500, 0}, {100, 0}, {300, 0}, {900, 0}});

    Order o = build(pts, 0, 4, Axis::Wire);
    CHECK((o == Order{0, 1, 2, 3}), "ось Wire не меняет порядок");

    Order n(4);
    buildOrder(nullptr, 0, 0, 4, Axis::X, n.data());
    CHECK((n == Order{0, 1, 2, 3}), "без раскладки порядок по проводу");

    Order e = build({}, 0, 4, Axis::X);
    CHECK((e == Order{0, 1, 2, 3}), "пустая раскладка — порядок по проводу");

    Order one = build(pts, 2, 3, Axis::X);
    CHECK((one == Order{0}), "сегмент из одного диода");

    Order none(1, 99);
    buildOrder(pts.data(), 4, 3, 3, Axis::X, none.data());
    CHECK(none[0] == 99, "пустой сегмент ничего не пишет");
    buildOrder(pts.data(), 4, 3, 2, Axis::X, none.data());
    CHECK(none[0] == 99, "stop < start ничего не пишет");
}

// ------------------------------------------------------- оси

static void testAxes() {
    SECTION("оси X и Y");
    const auto px = make({{500, 0}, {100, 0}, {300, 0}, {900, 0}, {0, 0}});
    CHECK((build(px, 0, 5, Axis::X) == Order{4, 1, 2, 0, 3}), "X: слева направо");

    const auto py = make({{0, 700}, {0, 100}, {0, 400}, {0, 0}});
    CHECK((build(py, 0, 4, Axis::Y) == Order{3, 1, 2, 0}), "Y: сверху вниз");

    SECTION("равные ключи сохраняют порядок провода");
    const auto tie = make({{200, 5}, {100, 9}, {200, 1}, {100, 3}});
    CHECK((build(tie, 0, 4, Axis::X) == Order{1, 3, 0, 2}),
          "при равном x порядок по номеру диода");

    SECTION("центр");
    const auto pc = make({{0, 0}, {1000, 0}, {0, 1000}, {1000, 1000}, {500, 500}});
    CHECK((build(pc, 0, 5, Axis::Center) == Order{4, 0, 1, 2, 3}),
          "Center: центральный первым, угловые по порядку провода");

    SECTION("угол");
    // Экранные координаты: y растёт вниз, угол растёт по часовой стрелке.
    const auto pa = make({{1000, 500}, {500, 1000}, {0, 500}, {500, 0}});
    CHECK((build(pa, 0, 4, Axis::Angle) == Order{3, 0, 1, 2}),
          "Angle: верх, право, низ, лево");
}

// ------------------------------------------------------- границы сегмента

static void testSegments() {
    SECTION("сегмент внутри раскладки");
    const auto pts = make({{50, 0}, {40, 0}, {300, 0}, {100, 0}, {200, 0}, {10, 0}});
    CHECK((build(pts, 2, 5, Axis::X) == Order{1, 2, 0}),
          "сегмент [2,5) сортируется сам по себе");

    SECTION("центр считается по рамке сегмента");
    // Весь ряд: {0,0}, {100,0}, {200,0} в сегменте, а {1000,0} вне его.
    // Рамка сегмента 0..200, центр 100 — диод в середине идёт первым.
    const auto row = make({{0, 0}, {100, 0}, {200, 0}, {1000, 0}});
    CHECK((build(row, 0, 3, Axis::Center) == Order{1, 0, 2}),
          "центр сегмента, а не всей ленты");

    SECTION("раскладка короче сегмента");
    const auto shortL = make({{300, 0}, {100, 0}, {200, 0}});
    Order o(5);
    buildOrder(shortL.data(), 3, 0, 5, Axis::X, o.data());
    CHECK((o == Order{1, 2, 0, 3, 4}),
          "диоды без точки — в конец, в порядке провода");

    buildOrder(shortL.data(), 3, 3, 5, Axis::X, o.data());
    CHECK((Order(o.begin(), o.begin() + 2) == Order{0, 1}),
          "сегмент целиком за раскладкой — порядок по проводу");
}

// ------------------------------------------------------- перестановка кадра

static void testScatter() {
    SECTION("scatter");
    const int src[3] = {10, 20, 30};
    int       dst[3] = {0, 0, 0};
    const uint16_t order[3] = {2, 0, 1};
    scatter(src, dst, order, 3);
    CHECK(dst[2] == 10 && dst[0] == 20 && dst[1] == 30,
          "dst[order[v]] = src[v]");

    // Сквозная проверка: «бегущий фронт» по виртуальному порядку на
    // физических диодах идёт по возрастанию x.
    const auto pts = make({{500, 0}, {100, 0}, {300, 0}, {900, 0}, {0, 0}});
    const Order ord = build(pts, 0, 5, Axis::X);
    int ramp[5], phys[5];
    for (int v = 0; v < 5; ++v) ramp[v] = v;
    scatter(ramp, phys, ord.data(), 5);
    bool mono = true;
    for (size_t i = 0; i < pts.size(); ++i)
        for (size_t j = 0; j < pts.size(); ++j)
            if (pts[i].x < pts[j].x && phys[i] >= phys[j]) mono = false;
    CHECK(mono, "ступень по ленте стала ступенью по оси X");
}

// ------------------------------------------------------- свойства

static void testProperties() {
    SECTION("перестановка, монотонность, детерминизм на случайных данных");
    const Axis axes[] = {Axis::Wire, Axis::X, Axis::Y, Axis::Angle, Axis::Center};
    const size_t sizes[] = {2, 3, 17, 300, 1200};

    bool allPerm = true, deterministic = true, monoX = true, monoY = true;
    bool monoC = true, monoA = true;

    for (size_t n : sizes) {
        const auto pts = randomPoints(n);
        for (Axis a : axes) {
            const Order o1 = build(pts, 0, static_cast<uint16_t>(n), a);
            const Order o2 = build(pts, 0, static_cast<uint16_t>(n), a);
            if (!isPermutation(o1)) allPerm = false;
            if (o1 != o2) deterministic = false;

            int minX = 1 << 30, maxX = -1, minY = 1 << 30, maxY = -1;
            for (const auto& p : pts) {
                minX = std::min<int>(minX, p.x); maxX = std::max<int>(maxX, p.x);
                minY = std::min<int>(minY, p.y); maxY = std::max<int>(maxY, p.y);
            }
            const float cx = (minX + maxX) / 2.0f, cy = (minY + maxY) / 2.0f;

            for (size_t v = 1; v < n; ++v) {
                const Point& p = pts[o1[v - 1]];
                const Point& q = pts[o1[v]];
                if (a == Axis::X && p.x > q.x) monoX = false;
                if (a == Axis::Y && p.y > q.y) monoY = false;
                if (a == Axis::Center) {
                    const float dp = (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy);
                    const float dq = (q.x - cx) * (q.x - cx) + (q.y - cy) * (q.y - cy);
                    if (dp > dq) monoC = false;
                }
                if (a == Axis::Angle &&
                    std::atan2(p.y - cy, p.x - cx) > std::atan2(q.y - cy, q.x - cx))
                    monoA = false;
            }
        }
    }
    CHECK(allPerm, "результат всегда перестановка 0..len-1");
    CHECK(deterministic, "одинаковый вход — одинаковый результат");
    CHECK(monoX, "X: x не убывает");
    CHECK(monoY, "Y: y не убывает");
    CHECK(monoC, "Center: расстояние до центра не убывает");
    CHECK(monoA, "Angle: угол не убывает");

    SECTION("все точки в одном месте");
    const std::vector<Point> same(50, Point{500, 500});
    bool ident = true;
    for (Axis a : axes) {
        const Order o = build(same, 0, 50, a);
        for (uint16_t i = 0; i < 50; ++i)
            if (o[i] != i) ident = false;
    }
    CHECK(ident, "одинаковые точки не перемешиваются");
}

// ------------------------------------------------------- кэш

static void testCache() {
    SECTION("кэш перестановки");
    const auto pts = make({{500, 0}, {100, 0}, {300, 0}, {900, 0}});
    const uint16_t n = 4;
    OrderCache c;

    CHECK(!c.update(pts.data(), n, 1, 0, 4, Axis::Wire), "Wire: перестановка не нужна");
    CHECK(c.rebuilds() == 0, "для Wire таблица не строится");

    CHECK(c.update(pts.data(), n, 1, 0, 4, Axis::X), "X: перестановка нужна");
    CHECK(c.rebuilds() == 1, "первая таблица построена");
    const Order expect{1, 2, 0, 3};
    CHECK((Order(c.data(), c.data() + 4) == expect), "таблица верна");

    c.update(pts.data(), n, 1, 0, 4, Axis::X);
    CHECK(c.rebuilds() == 1, "тот же ключ — без пересчёта");

    c.update(pts.data(), n, 2, 0, 4, Axis::X);
    CHECK(c.rebuilds() == 2, "новая версия раскладки — пересчёт");
    c.update(pts.data(), n, 2, 0, 4, Axis::Y);
    CHECK(c.rebuilds() == 3, "другая ось — пересчёт");
    c.update(pts.data(), n, 2, 1, 4, Axis::Y);
    CHECK(c.rebuilds() == 4, "другие границы сегмента — пересчёт");

    CHECK(!c.update(pts.data(), n, 2, 1, 2, Axis::Y), "один диод — перестановка не нужна");
    CHECK(!c.update(nullptr, 0, 3, 0, 4, Axis::X), "нет раскладки — перестановка не нужна");
    CHECK(c.update(pts.data(), n, 4, 0, 4, Axis::X), "раскладка появилась — снова нужна");
}

// ------------------------------------------------------- имена осей

static void testNames() {
    SECTION("имена осей");
    const Axis axes[] = {Axis::Wire, Axis::X, Axis::Y, Axis::Angle, Axis::Center};
    bool roundTrip = true;
    for (Axis a : axes) {
        Axis back = Axis::Wire;
        if (!axisFromName(axisName(a), &back) || back != a) roundTrip = false;
    }
    CHECK(roundTrip, "axisFromName(axisName(a)) == a");

    Axis keep = Axis::Y;
    CHECK(!axisFromName("diagonal", &keep) && keep == Axis::Y,
          "неизвестное имя: false, значение не тронуто");
    CHECK(!axisFromName(nullptr, &keep), "nullptr вместо имени");
    CHECK(!axisFromName("x", nullptr), "nullptr вместо результата");
}

// ------------------------------------------------------- main

int main() {
    std::printf("Lumen — тесты раскладки");

    testIdentity();
    testAxes();
    testSegments();
    testScatter();
    testCache();
    testProperties();
    testNames();

    std::printf("\n----------------------------------------\n");
    std::printf("Пройдено %d из %d\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
