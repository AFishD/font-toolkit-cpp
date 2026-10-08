// ftk: WOFF2 container decoding — Brotli decompression plus reconstruction of
// the transformed glyf/loca and hmtx tables.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"

#include <cstdint>
#include <vector>

namespace ftk {

// Brotli-decompress `n` bytes into exactly `expectedSize` bytes.
std::vector<uint8_t> brotliDecompress(const uint8_t* p, size_t n, size_t expectedSize);

} // namespace ftk
