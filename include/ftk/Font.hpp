// ftk: Font — the high-level, thread-safe font object tying all tables
// together. Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"
#include "ftk/Sfnt.hpp"
#include "ftk/Tables.hpp"
#include "ftk/Glyphs.hpp"
#include "ftk/Cff.hpp"
#include "ftk/VarTables.hpp"
#include "ftk/Layout.hpp"
#include "ftk/Color.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ftk {

struct LayoutInfo; // layout/Layout.hpp
class LayoutTables;
class ColorTables;

enum class OutlineFormat { None, TrueType, CFF, CFF2 };

// Per-glyph metrics & path bundle (variety at optional coords)
struct GlyphData {
    Path path;
    double advance = 0;
    double lsb = 0;
    double width = 0;    // CFF charstring width when available
    bool hasWidth = false;
    Bounds bounds;
    bool ok = true;
    std::string name;
};

struct FontInfo {
    std::string path;
    std::string family, subfamily, fullName, postScriptName, version, uniqueID;
    std::string copyright, trademark, designer, description, manufacturer, license, licenseUrl;
    std::string typoFamily, typoSubfamily;

    uint16_t unitsPerEm = 1000;
    uint16_t numGlyphs = 0;
    int numFaces = 1;
    uint32_t faceIndex = 0;
    OutlineFormat outline = OutlineFormat::None;
    ContainerType container = ContainerType::Unknown;
    std::string flavor; // "TrueType", "CFF", "CFF2", ...

    Bounds bounds;
    int16_t ascender = 0, descender = 0, lineGap = 0;
    int16_t typoAscender = 0, typoDescender = 0, typoLineGap = 0;
    uint16_t winAscent = 0, winDescent = 0;
    uint16_t advanceWidthMax = 0;
    uint16_t weightClass = 400, widthClass = 5;
    uint16_t fsSelection = 0, fsType = 0;
    bool isBold = false, isItalic = false, isOblique = false, isRegular = false;
    bool fixedPitch = false;
    double italicAngle = 0;
    std::string vendor;
    uint8_t panose[10] = {0};

    size_t cmapMappings = 0;
    uint32_t cmapMin = 0, cmapMax = 0; // unicode range
    int cmapBestFormat = 0;
    bool hasVertical = false;
    bool isVariable = false;
    size_t numAxes = 0, numInstances = 0;
    std::vector<Tag> tableTags;
    uint64_t fileSize = 0;
    bool checksumsOk = true;
    bool checksumsVerified = true;
    std::string checksumError;
};

class Font {
public:
    // Opens from memory (takes ownership) or file. ttcIndex selects a face.
    static std::unique_ptr<Font> open(std::vector<uint8_t> data, uint32_t ttcIndex = 0);
    static std::unique_ptr<Font> openFile(const std::string& utf8Path, uint32_t ttcIndex = 0);
    static int numFacesInFile(const std::string& utf8Path);

    // computeChecksums forces a full checksum pass (extra read of every table);
    // leave false for batch scans.
    FontInfo info(bool computeChecksums = false) const;
    const std::string& path() const { return path_; }

    // ---- tables (all lazily parsed; thread-safe) ----
    const Head* head() const;
    const Hhea* hhea() const;
    const Maxp* maxp() const;
    const Hmtx* hmtx() const;
    const Os2* os2() const;
    const Post* post() const;
    const NameTable* nameTable() const;
    const Cmap* cmap() const;
    const Gasp* gasp() const;
    const Kern* kern() const;
    const Hhea* vhea() const;
    const Hmtx* vmtx() const;
    const VORG* vorg() const;
    const Fvar* fvar() const;
    const Avar* avar() const;
    const Stat* stat() const;
    const Gvar* gvar() const;
    const Hvar* hvar() const;
    const Hvar* vvar() const;
    const GlyfTable* glyf() const;
    const CffFont* cff() const;

    const LayoutTables* layout() const; // GDEF/GSUB/GPOS
    const ColorTables* color() const;   // COLR/CPAL/sbix/CBDT/SVG

    // ---- raw ----
    std::vector<Tag> tableTags() const;
    bool hasTable(Tag t) const;
    const uint8_t* rawTable(Tag t, size_t& len) const;
    std::vector<uint8_t> rawTableCopy(Tag t) const;
    const SfntFont& sfnt() const { return *sfnt_; }
    bool verifyChecksums(std::string* err) const;

    // ---- glyphs ----
    uint16_t numGlyphs() const;
    uint16_t glyphForUnicode(uint32_t cp) const;
    uint32_t unicodeForGlyph(uint16_t gid) const; // best-effort reverse
    std::string glyphName(uint16_t gid) const;
    uint16_t glyphForName(const std::string& name) const;
    std::vector<uint16_t> glyphIdsForUnicodes(const std::vector<uint32_t>& cps) const;
    void compositeClosure(const std::vector<uint16_t>& gids, std::vector<bool>& keep) const;

    OutlineFormat outlineFormat() const;

    // Full glyph data at optional variable-font user coords.
    GlyphData glyph(uint16_t gid, const std::vector<double>* userCoords = nullptr) const;
    Path glyphPath(uint16_t gid, const std::vector<double>* userCoords = nullptr) const;
    // Same as glyphPath but reuses the caller's Path buffer (no allocation
    // churn, skips glyph-name/metrics work) — use in hot batch loops.
    void glyphPathInto(uint16_t gid, Path& out,
                       const std::vector<double>* userCoords = nullptr) const;
    FBounds glyphBoundsF(uint16_t gid, const std::vector<double>* userCoords = nullptr) const;
    Bounds glyphBounds(uint16_t gid, const std::vector<double>* userCoords = nullptr) const;
    double advance(uint16_t gid, const std::vector<double>* userCoords = nullptr) const;
    double verticalAdvance(uint16_t gid, const std::vector<double>* userCoords = nullptr) const;
    double kerning(uint16_t left, uint16_t right) const; // kern table + GPOS

    // ---- variable ----
    bool isVariable() const;
    std::vector<double> defaultCoords() const;
    // normalize user coords through fvar + avar
    std::vector<double> normalizeCoords(const std::vector<double>& userCoords) const;

    const std::vector<uint8_t>& fileBytes() const;

private:
    Font() = default;
    friend class FontFactory;

    std::vector<uint8_t> fileData_;
    std::unique_ptr<SfntFont> sfnt_;
    std::string path_;

    // lazy caches
    mutable std::recursive_mutex mu_;
    mutable std::unique_ptr<Head> head_;
    mutable std::unique_ptr<Hhea> hhea_, vhea_;
    mutable std::unique_ptr<Maxp> maxp_;
    mutable std::unique_ptr<Hmtx> hmtx_, vmtx_;
    mutable std::unique_ptr<Os2> os2_;
    mutable std::unique_ptr<Post> post_;
    mutable std::unique_ptr<NameTable> name_;
    mutable std::unique_ptr<Cmap> cmap_;
    mutable std::unique_ptr<Gasp> gasp_;
    mutable std::unique_ptr<Kern> kern_;
    mutable std::unique_ptr<VORG> vorg_;
    mutable std::unique_ptr<Fvar> fvar_;
    mutable std::unique_ptr<Avar> avar_;
    mutable std::unique_ptr<Stat> stat_;
    mutable std::unique_ptr<Gvar> gvar_;
    mutable std::unique_ptr<Hvar> hvar_, vvar_;
    mutable std::unique_ptr<GlyfTable> glyf_;
    mutable std::unique_ptr<CffFont> cff_;
    mutable std::unique_ptr<LayoutTables> layout_;
    mutable std::unique_ptr<ColorTables> color_;

    // composite composition
    void compose(uint16_t gid, const double m[6], const std::vector<double>* norm,
                 Path& out, std::vector<std::pair<double, double>>* accPoints, int depth) const;
    void composePointPass(uint16_t gid, const double m[6],
                          std::vector<std::pair<double, double>>& pts, int depth) const;
    void composeCff(uint16_t gid, const double m[6], const std::vector<double>* norm,
                    Path& out, int depth) const;
    CffGlyphInfo cffGlyph(uint16_t gid, const std::vector<double>* norm) const;
};

// helper: apply affine [a b c d e f] to a point
inline void xformPt(const double m[6], double& x, double& y) {
    double nx = m[0] * x + m[2] * y + m[4];
    double ny = m[1] * x + m[3] * y + m[5];
    x = nx;
    y = ny;
}

} // namespace ftk
