// ftk: SFNT container — plain sfnt (TTF/OTF), TTC collections, WOFF1.
// WOFF2 support hooks in (decoded via brotli when FTK_HAVE_BROTLI).
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ftk {

enum class ContainerType { Unknown, Sfnt, Ttc, Woff, Woff2 };

struct TableRec {
    Tag tag = 0;
    uint32_t offset = 0;      // absolute offset within resolved buffer
    uint32_t length = 0;      // decoded length
    uint32_t checksum = 0;    // checksum stored in directory / woff origChecksum
    uint32_t compLength = 0;  // woff: compressed length
    uint32_t origLength = 0;  // woff: declared original length
    uint8_t origFlags = 0;    // woff2: original flags (transformed bit etc.)
};

// One font face, with all tables resolved to a contiguous plain-sfnt layout.
class SfntFont {
public:
    // Takes ownership of fileData. faceIndex selects a face in a collection.
    // Throws ftk::Error on malformed input.
    static std::unique_ptr<SfntFont> parse(std::vector<uint8_t> fileData, uint32_t faceIndex = 0,
                                           int* numFacesOut = nullptr);

    uint32_t sfntVersion() const { return sfntVersion_; }
    ContainerType container() const { return container_; }
    uint32_t faceIndex() const { return faceIndex_; }
    int numFaces() const { return numFaces_; }

    const std::vector<TableRec>& tables() const { return tables_; }
    const TableRec* find(Tag tag) const;
    bool has(Tag tag) const { return find(tag) != nullptr; }

    // Raw (decoded) table bytes; nullptr if missing.
    const uint8_t* tableData(Tag tag, size_t& len) const;
    Reader tableReader(Tag tag) const; // throws if missing
    std::vector<Tag> tableTags() const; // sorted

    // Whole resolved font as a plain sfnt byte stream (for convert).
    const std::vector<uint8_t>& resolvedBytes() const;

    // Whole original file bytes (whatever container they came in).
    const std::vector<uint8_t>& originalBytes() const { return fileData_; }

    // checksum verification: returns false if a table checksum mismatches
    bool verifyChecksums(std::string* firstMismatch = nullptr) const;

private:
    SfntFont() = default;
    void parseSfntDirectory(Reader r, uint32_t base);
    void loadWoff1(Reader r);
    void loadWoff2(Reader r);

    std::vector<uint8_t> fileData_;   // original file
    std::vector<uint8_t> recon_;      // reconstructed plain sfnt (woff/woff2/ttc-dir)
    const uint8_t* buf_ = nullptr;    // resolved buffer base
    size_t bufSize_ = 0;
    uint32_t sfntVersion_ = 0;
    ContainerType container_ = ContainerType::Unknown;
    uint32_t faceIndex_ = 0;
    int numFaces_ = 1;
    std::vector<TableRec> tables_; // sorted by tag
};

// Detect (without full parse) whether bytes look like a font container.
enum class SniffResult { NotFont, Sfnt, Ttc, Woff, Woff2, Type1Pfb, Eot };
SniffResult sniffFont(const uint8_t* p, size_t n);

} // namespace ftk
