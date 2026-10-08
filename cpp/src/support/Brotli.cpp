#include "ftk/Brotli.hpp"

#include <brotli/decode.h>

namespace ftk {

std::vector<uint8_t> brotliDecompress(const uint8_t* p, size_t n, size_t expectedSize) {
    std::vector<uint8_t> out(expectedSize);
    size_t outSize = expectedSize;
    BrotliDecoderResult res = BrotliDecoderDecompress(n, p, &outSize, out.data());
    if (res != BROTLI_DECODER_RESULT_SUCCESS)
        fail("brotli: decompression failed (result " + std::to_string((int)res) + ")");
    if (outSize != expectedSize)
        fail("brotli: decompressed size mismatch (" + std::to_string(outSize) + " vs " +
             std::to_string(expectedSize) + ")");
    return out;
}

} // namespace ftk
