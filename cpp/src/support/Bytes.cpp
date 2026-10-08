#include "ftk/Bytes.hpp"

namespace ftk {

std::string tagToStr(Tag t) {
    std::string s;
    s += char(uint8_t(t >> 24));
    s += char(uint8_t(t >> 16));
    s += char(uint8_t(t >> 8));
    s += char(uint8_t(t));
    return s;
}

Tag strToTag(const std::string& s) {
    if (s.empty() || s.size() > 4) fail("tag must be 1..4 characters: '" + s + "'");
    std::string p = s;
    while (p.size() < 4) p += '.';
    return makeTag(p[0], p[1], p[2], p[3]);
}

uint32_t checksumBE(const uint8_t* p, size_t n) {
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
        sum += (uint32_t(p[i]) << 24) | (uint32_t(p[i + 1]) << 16) | (uint32_t(p[i + 2]) << 8) | p[i + 3];
    if (i < n) { // pad with zeros
        uint32_t v = 0;
        int shift = 24;
        for (; i < n; i++, shift -= 8) v |= uint32_t(p[i]) << shift;
        sum += v;
    }
    return sum;
}

} // namespace ftk
