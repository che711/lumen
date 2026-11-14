#include "layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace lumen {
namespace layout {

const char* axisName(Axis axis) {
    switch (axis) {
        case Axis::X:      return "x";
        case Axis::Y:      return "y";
        case Axis::Angle:  return "angle";
        case Axis::Center: return "center";
        default:           return "wire";
    }
}

bool axisFromName(const char* name, Axis* out) {
    if (name == nullptr || out == nullptr) return false;
    static const struct {
        const char* name;
        Axis        axis;
    } kAxes[] = {
        {"wire", Axis::Wire},   {"x", Axis::X},           {"y", Axis::Y},
        {"angle", Axis::Angle}, {"center", Axis::Center},
    };
    for (const auto& e : kAxes) {
        if (std::strcmp(name, e.name) == 0) {
            *out = e.axis;
            return true;
        }
    }
    return false;
}

void buildOrder(const Point* pts, uint16_t count, uint16_t start,
                uint16_t stop, Axis axis, uint16_t* order) {
    const uint16_t len = stop > start ? static_cast<uint16_t>(stop - start) : 0;
    for (uint16_t i = 0; i < len; ++i) order[i] = i;
    if (len < 2 || axis == Axis::Wire || pts == nullptr) return;

    // Рамка по диодам сегмента, для которых точка известна.
    int  minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool any = false;
    for (uint16_t i = 0; i < len; ++i) {
        const uint32_t idx = static_cast<uint32_t>(start) + i;
        if (idx >= count) break;
        const Point& p = pts[idx];
        if (!any) {
            minX = maxX = p.x;
            minY = maxY = p.y;
            any = true;
        } else {
            minX = std::min<int>(minX, p.x);
            maxX = std::max<int>(maxX, p.x);
            minY = std::min<int>(minY, p.y);
            maxY = std::max<int>(maxY, p.y);
        }
    }
    if (!any) return;

    const float cx = (minX + maxX) / 2.0f;
    const float cy = (minY + maxY) / 2.0f;

    struct Item {
        float    key;
        uint16_t off;
    };
    std::vector<Item> items;
    items.reserve(len);

    for (uint16_t i = 0; i < len; ++i) {
        const uint32_t idx = static_cast<uint32_t>(start) + i;
        float key = std::numeric_limits<float>::infinity();  // нет точки — в конец
        if (idx < count) {
            const float x = pts[idx].x, y = pts[idx].y;
            switch (axis) {
                case Axis::X:      key = x; break;
                case Axis::Y:      key = y; break;
                case Axis::Angle:  key = std::atan2(y - cy, x - cx); break;
                case Axis::Center: key = (x - cx) * (x - cx) + (y - cy) * (y - cy); break;
                default: break;
            }
        }
        items.push_back(Item{key, i});
    }

    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.key < b.key; });

    for (uint16_t v = 0; v < len; ++v) order[v] = items[v].off;
}

bool OrderCache::update(const Point* pts, uint16_t count, uint32_t version,
                        uint16_t start, uint16_t stop, Axis axis) {
    if (haveKey_ && axis == axis_ && start == start_ && stop == stop_ &&
        count == count_ && version == version_) {
        return active_;
    }

    haveKey_ = true;
    axis_ = axis;
    start_ = start;
    stop_ = stop;
    count_ = count;
    version_ = version;

    const uint16_t len = stop > start ? static_cast<uint16_t>(stop - start) : 0;
    active_ = axis != Axis::Wire && pts != nullptr && count > 0 && len >= 2;
    if (active_) {
        order_.resize(len);
        buildOrder(pts, count, start, stop, axis, order_.data());
        rebuilds_++;
    }
    return active_;
}

}  // namespace layout
}  // namespace lumen
