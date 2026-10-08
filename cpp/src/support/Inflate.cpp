#include "ftk/Inflate.hpp"
#include "ftk/Bytes.hpp"

#include <cstring>

namespace ftk {

namespace {

// Simple LSB bit reader over big buffer.
struct BitIn {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t acc = 0;
    int nbits = 0;

    BitIn(const uint8_t* d, const uint8_t* e) : p(d), end(e) {}

    uint32_t get(int n) { // n <= 16 (25 would break; we chunk)
        while (nbits < n) {
            if (p >= end) fail("deflate: out of input");
            acc |= uint32_t(*p++) << nbits;
            nbits += 8;
        }
        uint32_t v = acc & ((1u << n) - 1);
        acc >>= n;
        nbits -= n;
        return v;
    }
    void alignByte() {
        int drop = nbits & 7;
        acc >>= drop;
        nbits -= drop;
    }
    bool byteAlignedEmpty() const { return nbits == 0 && p >= end; }
};

struct Huff {
    // Canonical Huffman decoder (max 15 bits).
    std::vector<uint16_t> counts;  // len -> number of codes
    std::vector<uint16_t> symbols; // symbols in canonical order
    bool built = false;

    void build(const uint8_t* lengths, int n) {
        counts.assign(16, 0);
        for (int i = 0; i < n; i++) counts[lengths[i]]++;
        counts[0] = 0;
        // check over-subscription
        int left = 1;
        for (int l = 1; l <= 15; l++) {
            left <<= 1;
            left -= counts[l];
            if (left < 0) fail("deflate: over-subscribed huffman code");
        }
        std::vector<uint16_t> offs(16, 0);
        for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + counts[l];
        symbols.assign(n, 0);
        for (int i = 0; i < n; i++)
            if (lengths[i]) symbols[offs[lengths[i]]++] = uint16_t(i);
        built = true;
    }

    int decode(BitIn& in) const {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l <= 15; l++) {
            code |= (int)in.get(1);
            int count = counts[l];
            if (code - first < count) return symbols[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        fail("deflate: invalid huffman code");
        return -1;
    }
};

const uint16_t kLenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const uint8_t  kLenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const uint16_t kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const uint8_t  kDistExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

void buildFixed(Huff& lit, Huff& dist) {
    uint8_t lengths[288];
    for (int i = 0; i < 144; i++) lengths[i] = 8;
    for (int i = 144; i < 256; i++) lengths[i] = 9;
    for (int i = 256; i < 280; i++) lengths[i] = 7;
    for (int i = 280; i < 288; i++) lengths[i] = 8;
    lit.build(lengths, 288);
    uint8_t dl[30];
    for (int i = 0; i < 30; i++) dl[i] = 5;
    dist.build(dl, 30);
}

void readDynamic(BitIn& in, Huff& lit, Huff& dist) {
    const int order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int hlit = (int)in.get(5) + 257;
    int hdist = (int)in.get(5) + 1;
    int hclen = (int)in.get(4) + 4;
    uint8_t clen[19] = {0};
    for (int i = 0; i < hclen; i++) clen[order[i]] = uint8_t(in.get(3));
    Huff clh;
    clh.build(clen, 19);

    std::vector<uint8_t> lengths(hlit + hdist, 0);
    int i = 0;
    while (i < hlit + hdist) {
        int sym = clh.decode(in);
        if (sym < 16) {
            lengths[i++] = uint8_t(sym);
        } else if (sym == 16) {
            if (i == 0) fail("deflate: repeat with no previous length");
            uint8_t prev = lengths[i - 1];
            int rep = 3 + (int)in.get(2);
            while (rep-- && i < hlit + hdist) lengths[i++] = prev;
        } else if (sym == 17) {
            int rep = 3 + (int)in.get(3);
            while (rep-- && i < hlit + hdist) lengths[i++] = 0;
        } else {
            int rep = 11 + (int)in.get(7);
            while (rep-- && i < hlit + hdist) lengths[i++] = 0;
        }
    }
    if (lengths[256] == 0) fail("deflate: missing end-of-block code");
    lit.build(lengths.data(), hlit);
    dist.build(lengths.data() + hlit, hdist);
}

void inflateBlock(BitIn& in, const Huff& lit, const Huff& dist, std::vector<uint8_t>& out) {
    for (;;) {
        int sym = lit.decode(in);
        if (sym < 256) {
            out.push_back(uint8_t(sym));
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) fail("deflate: invalid length symbol");
            size_t len = kLenBase[sym] + in.get(kLenExtra[sym]);
            int dsym = dist.decode(in);
            if (dsym >= 30) fail("deflate: invalid distance symbol");
            size_t d = kDistBase[dsym] + in.get(kDistExtra[dsym]);
            if (d > out.size()) fail("deflate: distance too far back");
            size_t from = out.size() - d;
            for (size_t k = 0; k < len; k++) out.push_back(out[from + k]);
        }
    }
}

} // namespace

std::vector<uint8_t> inflateRaw(const uint8_t* p, size_t n, size_t sizeHint) {
    BitIn in(p, p + n);
    std::vector<uint8_t> out;
    if (sizeHint && sizeHint < (size_t(1) << 30)) out.reserve(sizeHint);
    for (;;) {
        int finalBit = (int)in.get(1);
        int type = (int)in.get(2);
        if (type == 0) {
            in.alignByte();
            if (in.p + 4 > in.end) fail("deflate: truncated stored block");
            size_t len = in.p[0] | (in.p[1] << 8);
            size_t nlen = in.p[2] | (in.p[3] << 8);
            in.p += 4;
            if ((len ^ 0xFFFF) != nlen) fail("deflate: stored block length mismatch");
            if (in.p + len > in.end) fail("deflate: stored block overruns input");
            out.insert(out.end(), in.p, in.p + len);
            in.p += len;
        } else if (type == 1) {
            Huff lit, dist;
            buildFixed(lit, dist);
            inflateBlock(in, lit, dist, out);
        } else if (type == 2) {
            Huff lit, dist;
            readDynamic(in, lit, dist);
            inflateBlock(in, lit, dist, out);
        } else {
            fail("deflate: invalid block type 3");
        }
        if (finalBit) break;
    }
    return out;
}

uint32_t adler32(const uint8_t* p, size_t n, uint32_t start) {
    const uint32_t MOD = 65521;
    uint32_t a = start & 0xFFFF, b = (start >> 16) & 0xFFFF;
    size_t i = 0;
    while (i < n) {
        size_t chunk = n - i < 5552 ? n - i : 5552;
        for (size_t k = 0; k < chunk; k++) {
            a += p[i + k];
            b += a;
        }
        a %= MOD;
        b %= MOD;
        i += chunk;
    }
    return (b << 16) | a;
}

std::vector<uint8_t> zlibDecompress(const uint8_t* p, size_t n, size_t sizeHint) {
    if (n < 6) fail("zlib: too short");
    uint8_t cmf = p[0], flg = p[1];
    if ((cmf & 0x0F) != 8) fail("zlib: unsupported compression method");
    if (((uint32_t(cmf) << 8) | flg) % 31 != 0) fail("zlib: bad header check");
    size_t off = 2;
    if (flg & 0x20) { // FDICT preset dictionary
        off += 4;
        fail("zlib: preset dictionary not supported");
    }
    std::vector<uint8_t> out = inflateRaw(p + off, n - off - 4, sizeHint);
    // verify adler32 (big-endian trailer)
    const uint8_t* q = p + n - 4;
    uint32_t want = (uint32_t(q[0]) << 24) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 8) | q[3];
    uint32_t got = adler32(out.data(), out.size());
    if (want != got) fail("zlib: adler32 mismatch");
    return out;
}

std::vector<uint8_t> zlibCompressStored(const uint8_t* p, size_t n) {
    std::vector<uint8_t> out;
    out.reserve(n + n / 65535 * 5 + 16);
    out.push_back(0x78); // CMF: deflate, 32K window
    out.push_back(0x01); // FLG: check bits valid, no dict, fastest
    size_t i = 0;
    if (n == 0) {
        out.push_back(1); // final, stored
        out.push_back(0); out.push_back(0); out.push_back(0xFF); out.push_back(0xFF);
    }
    while (i < n) {
        size_t chunk = n - i < 65535 ? n - i : 65535;
        bool final_ = (i + chunk) >= n;
        out.push_back(final_ ? 1 : 0);
        out.push_back(uint8_t(chunk & 0xFF));
        out.push_back(uint8_t(chunk >> 8));
        out.push_back(uint8_t(~chunk & 0xFF));
        out.push_back(uint8_t((~chunk >> 8) & 0xFF));
        out.insert(out.end(), p + i, p + i + chunk);
        i += chunk;
    }
    uint32_t a = adler32(p, n);
    out.push_back(uint8_t(a >> 24)); out.push_back(uint8_t(a >> 16));
    out.push_back(uint8_t(a >> 8)); out.push_back(uint8_t(a));
    return out;
}

} // namespace ftk
