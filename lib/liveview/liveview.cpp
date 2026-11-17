#include "liveview.h"

#include <cstring>

namespace lumen {
namespace liveview {

size_t packFrame(const uint8_t* rgb, uint16_t count, uint8_t* out) {
    out[0] = kFrameType;
    out[1] = static_cast<uint8_t>(count >> 8);
    out[2] = static_cast<uint8_t>(count & 0xFF);
    if (count) std::memcpy(out + kHeaderBytes, rgb, 3u * count);
    return frameBytes(count);
}

}  // namespace liveview
}  // namespace lumen
