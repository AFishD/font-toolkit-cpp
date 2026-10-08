#include "ftk/Sfnt.hpp"
#include "ftk/Inflate.hpp"

#include <algorithm>

namespace ftk {

SniffResult sniffFont(const uint8_t* p, size_t n) {
    if (n < 4) return SniffResult::NotFont;
    uint32_t t = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    if (t == 0x00010000 || t == makeTag('O','T','T','O') || t == makeTag('t','r','u','e') || t == makeTag('t','y','p','1'))
        return SniffResult::Sfnt;
    if (t == makeTag('t','t','c','f')) return SniffResult::Ttc;
    if (t == makeTag('w','O','F','F')) return n >= 8 && p[4] == 0 ? SniffResult::Woff : SniffResult::NotFont;
    if (t == makeTag('w','O','F','2')) return n >= 8 && p[4] == 0 ? SniffResult::Woff2 : SniffResult::NotFont;
    if (p[0] == 0x80 && p[1] == 0x01) return SniffResult::Type1Pfb;
    if (n >= 36 && t == 0x00010000) return SniffResult::Sfnt;
    if (n >= 44 && t == 0x00020000) return SniffResult::Eot; // EOT magic 0x00020000-ish
    return SniffResult::NotFont;
}

const TableRec* SfntFont::find(Tag tag) const {
    auto it = std::lower_bound(tables_.begin(), tables_.end(), tag,
                               [](const TableRec& t, Tag g) { return t.tag < g; });
    if (it != tables_.end() && it->tag == tag) return &*it;
    return nullptr;
}

std::vector<Tag> SfntFont::tableTags() const {
    std::vector<Tag> v;
    v.reserve(tables_.size());
    for (auto& t : tables_) v.push_back(t.tag);
    return v;
}

const uint8_t* SfntFont::tableData(Tag tag, size_t& len) const {
    const TableRec* t = find(tag);
    if (!t) { len = 0; return nullptr; }
    len = t->length;
    if (t->offset > bufSize_ || t->length > bufSize_ - t->offset)
        fail("table '" + tagToStr(tag) + "' out of bounds in font");
    return buf_ + t->offset;
}

Reader SfntFont::tableReader(Tag tag) const {
    size_t len;
    const uint8_t* p = tableData(tag, len);
    if (!p) fail("missing table '" + tagToStr(tag) + "'");
    return Reader(p, len);
}

const std::vector<uint8_t>& SfntFont::resolvedBytes() const {
    return container_ == ContainerType::Sfnt || container_ == ContainerType::Unknown ? fileData_ : recon_;
}

void SfntFont::parseSfntDirectory(Reader r, uint32_t base) {
    sfntVersion_ = r.u32();
    uint16_t numTables = r.u16();
    r.skip(6);
    tables_.clear();
    tables_.reserve(numTables);
    for (uint16_t i = 0; i < numTables; i++) {
        TableRec t;
        t.tag = r.u32();
        t.checksum = r.u32();
        uint32_t off = r.u32();
        t.length = r.u32();
        t.offset = base + off;
        if (t.offset < base) fail("table offset overflow");
        tables_.push_back(t);
    }
    std::sort(tables_.begin(), tables_.end(), [](const TableRec& a, const TableRec& b) { return a.tag < b.tag; });
    // drop duplicates (keep first)
    tables_.erase(std::unique(tables_.begin(), tables_.end(),
                              [](const TableRec& a, const TableRec& b) { return a.tag == b.tag; }),
                  tables_.end());
}

void SfntFont::loadWoff1(Reader r) {
    // WOFF header: signature(4) flavor(4) length(4) numTables(2) reserved(2)
    // totalSfntSize(4) majorVersion(2) minorVersion(2) metaOffset(4)
    // metaLength(4) metaOrigLength(4) privOffset(4) privLength(4)
    r.seek(0);
    r.u32();                 // signature 'wOFF'
    sfntVersion_ = r.u32();  // flavor: the actual sfnt version
    r.u32();                 // total WOFF length
    uint16_t numTables = r.u16();
    uint16_t reserved = r.u16();
    if (reserved != 0) fail("WOFF: reserved field is not zero");
    r.u32(); // totalSfntSize
    r.u16(); r.u16(); // major/minor version
    r.u32(); r.u32(); r.u32(); // meta offset/length/origLength
    r.u32(); r.u32(); // private offset/length

    struct Entry { Tag tag; uint32_t offset, compLength, origLength, origChecksum; };
    std::vector<Entry> entries(numTables);
    for (uint16_t i = 0; i < numTables; i++) {
        Entry& e = entries[i];
        e.tag = r.u32();
        e.offset = r.u32();
        e.compLength = r.u32();
        e.origLength = r.u32();
        e.origChecksum = r.u32();
    }

    // Decompress tables; lay out a plain sfnt in recon_.
    Writer w;
    w.u32(sfntVersion_);
    w.u16(numTables);
    uint16_t entrySelector = numTables ? (uint16_t)(__builtin_popcount((unsigned)numTables - 1)) : 0;
    uint16_t searchRange = (uint16_t)(16 * (1u << entrySelector));
    w.u16(searchRange);
    w.u16(entrySelector);
    w.u16((uint16_t)(numTables * 16 - searchRange));
    size_t dirPos = w.offset();
    w.zeros((size_t)numTables * 16);

    std::vector<TableRec> recs(numTables);
    for (uint16_t i = 0; i < numTables; i++) {
        const Entry& e = entries[i];
        TableRec& t = recs[i];
        t.tag = e.tag;
        t.checksum = e.origChecksum;
        t.length = e.origLength;
        t.origLength = e.origLength;
        t.compLength = e.compLength;

        size_t pos = w.offset();
        if (e.offset > r.size() || e.compLength > r.size() - e.offset)
            fail("WOFF: table '" + tagToStr(e.tag) + "' out of bounds");
        if (e.compLength < e.origLength) {
            auto dec = zlibDecompress(r.data() + e.offset, e.compLength, e.origLength);
            if (dec.size() != e.origLength)
                fail("WOFF: table '" + tagToStr(e.tag) + "' decompressed size mismatch");
            w.bytes(dec);
        } else {
            w.bytes(r.data() + e.offset, e.origLength);
        }
        while (w.offset() % 4) w.u8(0);
        t.offset = (uint32_t)pos;
    }

    // Directory, sorted by tag.
    std::vector<uint16_t> order(numTables);
    for (uint16_t k = 0; k < numTables; k++) order[k] = k;
    std::sort(order.begin(), order.end(), [&](uint16_t a, uint16_t b) { return recs[a].tag < recs[b].tag; });
    for (uint16_t k = 0; k < numTables; k++) {
        const TableRec& t = recs[order[k]];
        size_t p = dirPos + (size_t)k * 16;
        w.patch32(p, t.tag);
        w.patch32(p + 4, t.checksum);
        w.patch32(p + 8, t.offset);
        w.patch32(p + 12, t.length);
    }

    recon_ = std::move(w.buf());
    buf_ = recon_.data();
    bufSize_ = recon_.size();
    container_ = ContainerType::Woff;
    parseSfntDirectory(Reader(recon_), 0);
}

bool SfntFont::verifyChecksums(std::string* firstMismatch) const {
    for (const auto& t : tables_) {
        if (t.offset > bufSize_ || t.length > bufSize_ - t.offset) {
            if (firstMismatch) *firstMismatch = "table '" + tagToStr(t.tag) + "' out of range";
            return false;
        }
        if (t.tag == tag_Head) {
            // head checksum is computed with checkSumAdjustment zeroed
            size_t len;
            const uint8_t* p = tableData(tag_Head, len);
            std::vector<uint8_t> tmp(p, p + len);
            if (tmp.size() >= 12) tmp[8] = tmp[9] = tmp[10] = tmp[11] = 0;
            if (checksumBE(tmp.data(), tmp.size()) != t.checksum) {
                if (firstMismatch) *firstMismatch = "table 'head' checksum mismatch";
                return false;
            }
            continue;
        }
        if (checksumBE(buf_ + t.offset, t.length) != t.checksum) {
            if (firstMismatch)
                *firstMismatch = "table '" + tagToStr(t.tag) + "' checksum mismatch";
            return false;
        }
    }
    return true;
}

// loadWoff2 is implemented in Woff2.cpp (Brotli + transform reconstruction).

std::unique_ptr<SfntFont> SfntFont::parse(std::vector<uint8_t> fileData, uint32_t faceIndex, int* numFacesOut) {
    auto f = std::unique_ptr<SfntFont>(new SfntFont());
    f->fileData_ = std::move(fileData);
    f->faceIndex_ = faceIndex;
    if (f->fileData_.size() < 12) fail("file too small to be a font");

    Reader r(f->fileData_);
    uint32_t magic = r.u32();
    switch (magic) {
    case 0x00010000:
    case makeTag('O','T','T','O'):
    case makeTag('t','r','u','e'):
    case makeTag('t','y','p','1'):
        f->container_ = ContainerType::Sfnt;
        f->buf_ = f->fileData_.data();
        f->bufSize_ = f->fileData_.size();
        f->parseSfntDirectory(Reader(f->fileData_), 0);
        f->numFaces_ = 1;
        break;
    case makeTag('t','t','c','f'): {
        f->container_ = ContainerType::Ttc;
        uint32_t version = r.u32();
        uint32_t numFonts = r.u32();
        f->numFaces_ = (int)numFonts;
        if (numFacesOut) *numFacesOut = (int)numFonts;
        if (faceIndex >= numFonts) fail("TTC face index out of range (" + std::to_string(faceIndex) + " >= " + std::to_string(numFonts) + ")");
        std::vector<uint32_t> offsets;
        for (uint32_t i = 0; i < numFonts; i++) offsets.push_back(r.u32());
        uint32_t off = offsets[faceIndex];
        if (off + 12 > f->fileData_.size()) fail("TTC face offset out of range");
        f->buf_ = f->fileData_.data();
        f->bufSize_ = f->fileData_.size();
        // TTC table offsets are absolute file offsets (not face-relative).
        f->parseSfntDirectory(Reader(f->fileData_.data() + off, f->fileData_.size() - off), 0);
        (void)version;
        break;
    }
    case makeTag('w','O','F','F'):
        f->sfntVersion_ = 0;
        f->loadWoff1(Reader(f->fileData_));
        break;
    case makeTag('w','O','F','2'):
        f->loadWoff2(Reader(f->fileData_));
        break;
    default:
        fail("not a recognized font container (magic 0x" + [] (uint32_t m) {
            char buf[16];
            snprintf(buf, sizeof buf, "%08X", m);
            return std::string(buf);
        }(magic) + ")");
    }
    if (numFacesOut && magic != makeTag('t','t','c','f')) *numFacesOut = f->numFaces_;
    return f;
}

} // namespace ftk
