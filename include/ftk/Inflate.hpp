// ftk: raw DEFLATE (RFC 1951) + zlib (RFC 1950) decompression and a
// minimal deflate compressor (stored blocks). Part of the Font_Toolkit
// C++ rewrite. No external dependencies.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace ftk {

// Inflate raw deflate stream. Returns decompressed bytes; throws ftk::Error.
std::vector<uint8_t> inflateRaw(const uint8_t* p, size_t n, size_t sizeHint = 0);

// zlib wrapper: 2-byte header + deflate + adler32.
std::vector<uint8_t> zlibDecompress(const uint8_t* p, size_t n, size_t sizeHint = 0);

// Minimal compressor: stored (uncompressed) deflate blocks + zlib wrapper.
// Valid zlib stream, zero compression — used for WOFF1 export.
std::vector<uint8_t> zlibCompressStored(const uint8_t* p, size_t n);

uint32_t adler32(const uint8_t* p, size_t n, uint32_t start = 1);

} // namespace ftk
