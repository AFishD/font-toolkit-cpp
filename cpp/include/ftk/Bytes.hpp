// ftk: Font Toolkit — big-endian binary reader/writer primitives.
// Part of the Font_Toolkit C++ rewrite. MIT-style license.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ftk {

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& msg) : std::runtime_error(msg) {}
};

inline void fail(const std::string& msg) { throw Error(msg); }

// ---------------------------------------------------------------------------
// Tags: 4-byte big-endian table/feature identifiers.
// ---------------------------------------------------------------------------

using Tag = uint32_t;

inline constexpr Tag makeTag(char a, char b, char c, char d) {
    return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
           (uint32_t(uint8_t(c)) << 8) | uint32_t(uint8_t(d));
}

#define FTK_TAG(a, b, c, d) ftk::makeTag(a, b, c, d)

std::string tagToStr(Tag t);
Tag strToTag(const std::string& s); // 1..4 chars, '.' padded

inline constexpr Tag tag_Head = makeTag('h','e','a','d');
inline constexpr Tag tag_Hhea = makeTag('h','h','e','a');
inline constexpr Tag tag_Maxp = makeTag('m','a','x','p');
inline constexpr Tag tag_Hmtx = makeTag('h','m','t','x');
inline constexpr Tag tag_Cmap = makeTag('c','m','a','p');
inline constexpr Tag tag_Name = makeTag('n','a','m','e');
inline constexpr Tag tag_Os2  = makeTag('O','S','/','2');
inline constexpr Tag tag_Post = makeTag('p','o','s','t');
inline constexpr Tag tag_Glyf = makeTag('g','l','y','f');
inline constexpr Tag tag_Loca = makeTag('l','o','c','a');
inline constexpr Tag tag_CFF  = makeTag('C','F','F',' ');
inline constexpr Tag tag_CFF2 = makeTag('C','F','F','2');
inline constexpr Tag tag_Vhea = makeTag('v','h','e','a');
inline constexpr Tag tag_Vmtx = makeTag('v','m','t','x');
inline constexpr Tag tag_VORG = makeTag('V','O','R','G');
inline constexpr Tag tag_Kern = makeTag('k','e','r','n');
inline constexpr Tag tag_GSUB = makeTag('G','S','U','B');
inline constexpr Tag tag_GPOS = makeTag('G','P','O','S');
inline constexpr Tag tag_GDEF = makeTag('G','D','E','F');
inline constexpr Tag tag_BASE = makeTag('B','A','S','E');
inline constexpr Tag tag_Fvar = makeTag('f','v','a','r');
inline constexpr Tag tag_Avar = makeTag('a','v','a','r');
inline constexpr Tag tag_Gvar = makeTag('g','v','a','r');
inline constexpr Tag tag_Cvar = makeTag('c','v','a','r');
inline constexpr Tag tag_HVAR = makeTag('H','V','A','R');
inline constexpr Tag tag_VVAR = makeTag('V','V','A','R');
inline constexpr Tag tag_STAT = makeTag('S','T','A','T');
inline constexpr Tag tag_COLR = makeTag('C','O','L','R');
inline constexpr Tag tag_CPAL = makeTag('C','P','A','L');
inline constexpr Tag tag_CBDT = makeTag('C','B','D','T');
inline constexpr Tag tag_CBLC = makeTag('C','B','L','C');
inline constexpr Tag tag_EBDT = makeTag('E','B','D','T');
inline constexpr Tag tag_EBLC = makeTag('E','B','L','C');
inline constexpr Tag tag_Sbix = makeTag('s','b','i','x');
inline constexpr Tag tag_SVG_ = makeTag('S','V','G',' ');
inline constexpr Tag tag_Mora = makeTag('m','o','r','a');
inline constexpr Tag tag_Mort = makeTag('m','o','r','t');
inline constexpr Tag tag_Morx = makeTag('m','o','r','x');
inline constexpr Tag tag_Gasp = makeTag('g','a','s','p');
inline constexpr Tag tag_DSIG = makeTag('D','S','I','G');
inline constexpr Tag tag_Ltsh = makeTag('L','T','S','H');
inline constexpr Tag tag_Hdmx = makeTag('h','d','m','x');
inline constexpr Tag tag_Cvt_ = makeTag('c','v','t',' ');
inline constexpr Tag tag_Fpgm = makeTag('f','p','g','m');
inline constexpr Tag tag_Prep = makeTag('p','r','e','p');
inline constexpr Tag tag_Ebdt = makeTag('E','B','D','T');

// ---------------------------------------------------------------------------
// Reader: bounds-checked big-endian cursor over external memory.
// ---------------------------------------------------------------------------
class Reader {
public:
    Reader() : base_(nullptr), p_(nullptr), end_(nullptr) {}
    Reader(const uint8_t* data, size_t size) : base_(data), p_(data), end_(data + size) {}
    explicit Reader(const std::vector<uint8_t>& v)
        : base_(v.data()), p_(v.data()), end_(v.data() + v.size()) {}

    size_t offset() const { return (size_t)(p_ - base_); }
    size_t remaining() const { return (size_t)(end_ - p_); }
    bool eof() const { return p_ >= end_; }
    size_t size() const { return (size_t)(end_ - base_); }
    const uint8_t* data() const { return base_; }
    const uint8_t* curp() const { return p_; }
    operator bool() const { return base_ != nullptr; }

    bool seek(size_t off) {
        if (off > size()) return false;
        p_ = base_ + off;
        return true;
    }
    bool skip(size_t n) {
        if (remaining() < n) return false;
        p_ += n;
        return true;
    }

    uint8_t u8() { need(1); return *p_++; }
    uint16_t u16() { need(2); uint16_t v = (uint16_t)((uint16_t(p_[0]) << 8) | p_[1]); p_ += 2; return v; }
    int16_t i16() { return (int16_t)u16(); }
    uint32_t u24() { need(3); uint32_t v = (uint32_t(p_[0]) << 16) | (uint32_t(p_[1]) << 8) | p_[2]; p_ += 3; return v; }
    uint32_t u32() { need(4); uint32_t v = (uint32_t(p_[0]) << 24) | (uint32_t(p_[1]) << 16) | (uint32_t(p_[2]) << 8) | p_[3]; p_ += 4; return v; }
    int32_t i32() { return (int32_t)u32(); }
    uint64_t u64() { need(8); uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | p_[i]; p_ += 8; return v; }
    int64_t i64() { return (int64_t)u64(); }

    float fixed16() { return (float)((int32_t)u32()) / 65536.0f; }
    float f2dot14() { return (float)((int32_t)(int16_t)u16()) / 16384.0f; }
    int64_t longDateTime() { return i64(); }
    Tag tag4() { return u32(); }

    const uint8_t* bytes(size_t n) { need(n); const uint8_t* r = p_; p_ += n; return r; }
    void copyTo(void* dst, size_t n) { need(n); std::memcpy(dst, p_, n); p_ += n; }
    std::vector<uint8_t> take(size_t n) { need(n); auto r = std::vector<uint8_t>(p_, p_ + n); p_ += n; return r; }
    std::string str(size_t n) { need(n); std::string s((const char*)p_, n); p_ += n; return s; }

    // Sub-reader of [off, off+len) in absolute (buffer) coordinates.
    Reader subAt(size_t off, size_t len) const {
        if (off > size() || len > size() - off)
            fail("subAt out of bounds (off=" + std::to_string(off) + ", len=" +
                 std::to_string(len) + ", size=" + std::to_string(size()) + ")");
        return Reader(base_ + off, len);
    }
    Reader subFrom(size_t off) const { return subAt(off, size() - off); }
    Reader rest() const { return Reader(p_, (size_t)(end_ - p_)); }
    // Same base and size (so offset() stays absolute) but cursor at current pos.
    Reader fromCursor() const {
        Reader r(base_, (size_t)(end_ - base_));
        r.p_ = p_;
        return r;
    }

private:
    void need(size_t n) const {
        if ((size_t)(end_ - p_) < n)
            fail("read past end of data (need " + std::to_string(n) + " bytes, have " +
                 std::to_string((size_t)(end_ - p_)) + ")");
    }

    const uint8_t* base_;
    const uint8_t* p_;
    const uint8_t* end_;
};

// ---------------------------------------------------------------------------
// Writer: big-endian growable buffer.
// ---------------------------------------------------------------------------
class Writer {
public:
    std::vector<uint8_t>& buf() { return b_; }
    const std::vector<uint8_t>& buf() const { return b_; }
    size_t offset() const { return b_.size(); }

    void u8(uint8_t v) { b_.push_back(v); }
    void u16(uint16_t v) { b_.push_back(uint8_t(v >> 8)); b_.push_back(uint8_t(v)); }
    void i16(int16_t v) { u16((uint16_t)v); }
    void u24(uint32_t v) { b_.push_back(uint8_t(v >> 16)); b_.push_back(uint8_t(v >> 8)); b_.push_back(uint8_t(v)); }
    void u32(uint32_t v) { b_.push_back(uint8_t(v >> 24)); b_.push_back(uint8_t(v >> 16)); b_.push_back(uint8_t(v >> 8)); b_.push_back(uint8_t(v)); }
    void i32(int32_t v) { u32((uint32_t)v); }
    void u64(uint64_t v) { for (int i = 7; i >= 0; i--) b_.push_back(uint8_t(v >> (8 * i))); }
    void fixed16(double f) { i32((int32_t)std::lround(f * 65536.0)); }
    void f2dot14(double f) { i16((int16_t)std::lround(f * 16384.0)); }
    void tag4(Tag t) { u32(t); }
    void bytes(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b_.insert(b_.end(), q, q + n); }
    void bytes(const std::vector<uint8_t>& v) { bytes(v.data(), v.size()); }
    void zeros(size_t n) { b_.insert(b_.end(), n, 0); }
    void align(size_t a) { while (b_.size() % a) b_.push_back(0); }

    void patch16(size_t off, uint16_t v) { b_[off] = uint8_t(v >> 8); b_[off + 1] = uint8_t(v); }
    void patch24(size_t off, uint32_t v) { b_[off] = uint8_t(v >> 16); b_[off + 1] = uint8_t(v >> 8); b_[off + 2] = uint8_t(v); }
    void patch32(size_t off, uint32_t v) { b_[off] = uint8_t(v >> 24); b_[off + 1] = uint8_t(v >> 16); b_[off + 2] = uint8_t(v >> 8); b_[off + 3] = uint8_t(v); }

private:
    std::vector<uint8_t> b_;
};

// ---------------------------------------------------------------------------
// Misc helpers
// ---------------------------------------------------------------------------

uint32_t checksumBE(const uint8_t* p, size_t n); // sum of big-endian u32s, zero padded

} // namespace ftk
