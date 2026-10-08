// CFF/CFF2 parsing and Type2 charstring interpretation.
#include "ftk/Cff.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ftk {

// ---------------------------------------------------------------------------
// Standard strings (SID 0..390), from the CFF specification.
// ---------------------------------------------------------------------------

static const char* kCffStandardStrings[] = {
    ".notdef","space","exclam","quotedbl","numbersign","dollar","percent","ampersand",
    "quoteright","parenleft","parenright","asterisk","plus","comma","hyphen","period",
    "slash","zero","one","two","three","four","five","six","seven","eight","nine",
    "colon","semicolon","less","equal","greater","question","at","A","B","C","D","E",
    "F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y",
    "Z","bracketleft","backslash","bracketright","asciicircum","underscore","quoteleft",
    "a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r","s","t",
    "u","v","w","x","y","z","braceleft","bar","braceright","asciitilde","exclamdown",
    "cent","sterling","fraction","yen","florin","section","currency","quotesingle",
    "quotedblleft","guillemotleft","guilsinglleft","guilsinglright","fi","fl","endash",
    "dagger","daggerdbl","periodcentered","paragraph","bullet","quotesinglbase",
    "quotedblbase","quotedblright","guillemotright","ellipsis","perthousand",
    "questiondown","grave","acute","circumflex","tilde","macron","breve","dotaccent",
    "dieresis","ring","cedilla","hungarumlaut","ogonek","caron","emdash","AE",
    "ordfeminine","Lslash","Oslash","OE","ordmasculine","ae","dotlessi","lslash",
    "oslash","oe","germandbls","onesuperior","logicalnot","mu","trademark","Eth",
    "onehalf","plusminus","Thorn","onequarter","divide","brokenbar","degree","thorn",
    "threequarters","twosuperior","registered","minus","eth","multiply","threesuperior",
    "copyright","Aacute","Acircumflex","Adieresis","Agrave","Aring","Atilde","Ccedilla",
    "Eacute","Ecircumflex","Edieresis","Egrave","Iacute","Icircumflex","Idieresis",
    "Igrave","Ntilde","Oacute","Ocircumflex","Odieresis","Ograve","Otilde","Scaron",
    "Uacute","Ucircumflex","Udieresis","Ugrave","Yacute","Ydieresis","Zcaron","aacute",
    "acircumflex","adieresis","agrave","aring","atilde","ccedilla","eacute",
    "ecircumflex","edieresis","egrave","iacute","icircumflex","idieresis","igrave",
    "ntilde","oacute","ocircumflex","odieresis","ograve","otilde","scaron","uacute",
    "ucircumflex","udieresis","ugrave","yacute","ydieresis","zcaron","exclamsmall",
    "Hungarumlautsmall","dollaroldstyle","dollarsuperior","ampersandsmall","Acutesmall",
    "parenleftsuperior","parenrightsuperior","twodotenleader","onedotenleader",
    "zerooldstyle","oneoldstyle","twooldstyle","threeoldstyle","fouroldstyle",
    "fiveoldstyle","sixoldstyle","sevenoldstyle","eightoldstyle","nineoldstyle",
    "commasuperior","threequartersemdash","periodsuperior","questionsmall","asuperior",
    "bsuperior","centsuperior","dsuperior","esuperior","isuperior","lsuperior",
    "msuperior","nsuperior","osuperior","rsuperior","ssuperior","tsuperior","ff",
    "ffi","ffl","parenleftinferior","parenrightinferior","Circumflexsmall",
    "hyphensuperior","Gravesmall","Asmall","Bsmall","Csmall","Dsmall","Esmall",
    "Fsmall","Gsmall","Hsmall","Ismall","Jsmall","Ksmall","Lsmall","Msmall","Nsmall",
    "Osmall","Psmall","Qsmall","Rsmall","Ssmall","Tsmall","Usmall","Vsmall","Wsmall",
    "Xsmall","Ysmall","Zsmall","colonmonetary","onefitted","rupiah","Tildesmall",
    "exclamdownsmall","centoldstyle","Lslashsmall","Scaronsmall","Zcaronsmall",
    "Dieresissmall","Brevesmall","Caronsmall","Dotaccentsmall","Macronsmall",
    "figuredash","hypheninferior","Ogoneksmall","Ringsmall","Cedillasmall",
    "questiondownsmall","oneeighth","threeeighths","fiveeighths","seveneighths",
    "onethird","twothirds","zerosuperior","foursuperior","fivesuperior","sixsuperior",
    "sevensuperior","eightsuperior","ninesuperior","zeroinferior","oneinferior",
    "twoinferior","threeinferior","fourinferior","fiveinferior","sixinferior",
    "seveninferior","eightinferior","nineinferior","centinferior","dollarinferior",
    "periodinferior","commainferior","Agravesmall","Aacutesmall","Acircumflexsmall",
    "Atildesmall","Adieresissmall","Aringsmall","AEsmall","Ccedillasmall","Egravesmall",
    "Eacutesmall","Ecircumflexsmall","Edieresissmall","Igravesmall","Iacutesmall",
    "Icircumflexsmall","Idieresissmall","Ethsmall","Ntildesmall","Ogravesmall",
    "Oacutesmall","Ocircumflexsmall","Otildesmall","Odieresissmall","OEsmall",
    "Oslashsmall","Ugravesmall","Uacutesmall","Ucircumflexsmall","Udieresissmall",
    "Yacutesmall","Thornsmall","Ydieresissmall","001.000","001.001","001.002",
    "001.003","Black","Bold","Book","Light","Medium","Regular","Roman","Semibold"
};
static const size_t kNumCffStdStrings = sizeof(kCffStandardStrings) / sizeof(kCffStandardStrings[0]);

const char* cffStandardString(uint32_t sid) {
    if (sid < kNumCffStdStrings) return kCffStandardStrings[sid];
    return nullptr;
}
size_t cffStandardStringCount() { return kNumCffStdStrings; }

// Predefined charsets (gid -> SID)
static const uint16_t kExpertCharset[] = {
    0,1,229,230,231,232,233,234,235,236,237,238,13,14,15,99,239,240,241,242,243,244,
    245,246,247,248,27,28,249,250,251,252,253,254,255,256,257,258,259,260,261,262,
    263,264,265,266,109,110,267,268,269,270,271,272,273,274,275,276,277,278,279,280,
    281,282,283,284,285,286,287,288,289,290,291,292,293,294,295,296,297,298,299,300,
    301,302,303,304,305,306,307,308,309,310,311,312,313,314,315,316,317,318,158,155,
    163,319,320,321,322,323,324,325,326,150,164,169,327,328,329,330,331,332,333,334,
    335,336,337,338,339,340,341,342,343,344,345,346,347,348,349,350,351,352,353,354,
    355,356,357,358,359,360,361,362,363,364,365,366,367,368,369,370,371,372,373,374,
    375,376,377,378
};
static const uint16_t kExpertSubsetCharset[] = {
    0,1,231,232,235,236,237,238,13,14,15,99,239,240,241,242,243,244,245,246,247,248,
    27,28,249,250,251,253,254,255,256,257,258,259,260,261,262,263,264,265,266,109,
    110,267,268,269,270,272,300,301,302,305,314,315,158,155,163,320,321,322,323,324,
    325,326,150,164,169,327,328,329,330,331,332,333,334,335,336,337,338,339,340,341,
    342,343,344,345,346
};

// StandardEncoding (byte -> glyph name)
static const char* kStandardEncoding[] = {
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    "space","exclam","quotedbl","numbersign","dollar","percent","ampersand",
    "quoteright","parenleft","parenright","asterisk","plus","comma","hyphen","period",
    "slash","zero","one","two","three","four","five","six","seven","eight","nine",
    "colon","semicolon","less","equal","greater","question","at","A","B","C","D","E",
    "F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y",
    "Z","bracketleft","backslash","bracketright","asciicircum","underscore","quoteleft",
    "a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r","s","t",
    "u","v","w","x","y","z","braceleft","bar","braceright","asciitilde",".notdef",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    "exclamdown","cent","sterling","fraction","yen","florin","section","currency",
    "quotesingle","quotedblleft","guillemotleft","guilsinglleft","guilsinglright",
    "fi","fl",".notdef","endash","dagger","daggerdbl","periodcentered",".notdef",
    "paragraph","bullet","quotesinglbase","quotedblbase","quotedblright",
    "guillemotright","ellipsis","perthousand",".notdef","questiondown",".notdef",
    "grave","acute","circumflex","tilde","macron","breve","dotaccent","dieresis",
    ".notdef","ring","cedilla",".notdef","hungarumlaut","ogonek","caron","emdash",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    ".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",".notdef",
    "AE",".notdef","ordfeminine",".notdef",".notdef",".notdef",".notdef","Lslash",
    "Oslash","OE","ordmasculine",".notdef",".notdef",".notdef",".notdef",".notdef",
    "ae",".notdef",".notdef",".notdef","dotlessi",".notdef",".notdef","lslash",
    "oslash","oe","germandbls",".notdef",".notdef",".notdef",".notdef"
};

std::string standardEncodingName(uint8_t code) { return kStandardEncoding[code]; }

// ---------------------------------------------------------------------------
// CffIndex
// ---------------------------------------------------------------------------

void CffIndex::parse(Reader r, bool cff2) {
    buf_ = r.data();
    bufSize_ = r.size();
    uint32_t count = cff2 ? r.u32() : r.u16();
    count_ = count;
    if (count == 0) {
        dataStart_ = (uint32_t)r.offset();
        offs_.assign(1, dataStart_); // so endOffset() works
        return;
    }
    if (count > 4u * 1024 * 1024) fail("CFF INDEX: unreasonable count");
    uint8_t offSize = r.u8();
    if (offSize == 0 || offSize > 4) fail("CFF INDEX: bad offSize");

    auto readOff = [&]() -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < offSize; i++) v = (v << 8) | r.u8();
        return v;
    };
    offs_.clear();
    offs_.reserve((size_t)count + 1);
    uint32_t first = readOff();
    offs_.push_back(first);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t o = readOff();
        offs_.push_back(o);
    }
    dataStart_ = (uint32_t)r.offset(); // start of item data
}

uint32_t CffIndex::endOffset() const {
    // absolute offset just past the last item
    if (count_ == 0) return dataStart_;
    return dataStart_ + (offs_[count_] - offs_[0]);
}

const uint8_t* CffIndex::itemData(size_t i, size_t& len) const {
    if (i >= count_) fail("CFF INDEX: index out of range");
    uint32_t a = offs_[i], b = offs_[i + 1];
    if (a > b) fail("CFF INDEX: negative item length");
    size_t offA = dataStart_ + (a - offs_[0]);
    len = b - a;
    if (offA + len > bufSize_) fail("CFF INDEX: item out of bounds");
    return buf_ + offA;
}

std::vector<uint8_t> CffIndex::itemCopy(size_t i) const {
    size_t len;
    const uint8_t* p = itemData(i, len);
    return std::vector<uint8_t>(p, p + len);
}

// ---------------------------------------------------------------------------
// Dict
// ---------------------------------------------------------------------------

static double parseRealNibbles(Reader& r) {
    std::string s;
    bool done = false;
    while (!done && r.remaining() > 0) {
        uint8_t b = r.u8();
        for (int half = 0; half < 2; half++) {
            uint8_t nib = half == 0 ? (b >> 4) : (b & 0xF);
            switch (nib) {
            case 0xF: done = true; break;
            case 0xA: s += '.'; break;
            case 0xB: s += 'E'; break;
            case 0xC: s += "E-"; break;
            case 0xD: break; // reserved
            case 0xE: s += '-'; break;
            default: s += char('0' + nib); break;
            }
            if (done) break;
        }
    }
    return s.empty() ? 0.0 : strtod(s.c_str(), nullptr);
}

void Dict::parse(Reader r, bool cff2) {
    std::vector<DictNumber> stack;
    while (r.remaining() > 0) {
        uint8_t b0 = r.u8();
        // CFF1 operators are 0..21; CFF2 also uses 22..24 (e.g. 24 = vstore,
        // 22 = vsindex, 23 = blend in private dicts).
        bool isOperator = b0 <= 21 || (cff2 && b0 >= 22 && b0 <= 27);
        if (isOperator) {
            DictEntry e;
            if (b0 == 12) {
                if (r.remaining() < 1) fail("CFF DICT: truncated escaped operator");
                e.op = 12;
                e.op2 = r.u8();
            } else {
                e.op = b0;
            }
            e.operands = std::move(stack);
            stack.clear();
            entries.push_back(std::move(e));
        } else if (b0 == 28) {
            DictNumber n; n.isInt = true; n.i = r.i16(); stack.push_back(n);
        } else if (b0 == 29) {
            DictNumber n; n.isInt = true; n.i = r.i32(); stack.push_back(n);
        } else if (b0 == 30) {
            // real number (not used by CFF2, but tolerated by other parsers)
            DictNumber n; n.isInt = false; n.d = parseRealNibbles(r); stack.push_back(n);
        } else if (b0 == 255) {
            DictNumber n; n.isInt = false; n.d = r.fixed16(); stack.push_back(n);
        } else if (b0 >= 32 && b0 <= 246) {
            DictNumber n; n.isInt = true; n.i = (int32_t)b0 - 139; stack.push_back(n);
        } else if (b0 >= 247 && b0 <= 250) {
            DictNumber n; n.isInt = true;
            n.i = (int32_t)(b0 - 247) * 256 + r.u8() + 108;
            stack.push_back(n);
        } else if (b0 >= 251 && b0 <= 254) {
            DictNumber n; n.isInt = true;
            n.i = -(int32_t)(b0 - 251) * 256 - r.u8() - 108;
            stack.push_back(n);
        } else {
            fail("CFF DICT: invalid byte 0x" + [](uint8_t b) {
                char buf[8];
                snprintf(buf, sizeof buf, "%02X", b);
                return std::string(buf);
            }(b0));
        }
    }
}

const DictEntry* Dict::find(uint8_t op) const {
    for (auto& e : entries)
        if (e.op == op && e.op != 12) return &e;
    return nullptr;
}
const DictEntry* Dict::find2(uint8_t op, uint8_t op2) const {
    for (auto& e : entries)
        if (e.op == 12 && e.op2 == op2) return &e;
    return nullptr;
}

int64_t Dict::asInt(const DictEntry* e, size_t idx, int64_t def) const {
    if (!e || idx >= e->operands.size()) return def;
    const DictNumber& n = e->operands[idx];
    return n.isInt ? n.i : (int64_t)std::lround(n.d);
}
double Dict::asNum(const DictEntry* e, size_t idx, double def) const {
    if (!e || idx >= e->operands.size()) return def;
    const DictNumber& n = e->operands[idx];
    return n.isInt ? (double)n.i : n.d;
}

void PrivateDict::parse(Reader r, bool cff2) {
    Dict d;
    d.parse(r, cff2);
    if (const DictEntry* e = d.find(19)) subrs = d.asInt(e, 0, -1);
    if (const DictEntry* e = d.find(20)) defaultWidthX = d.asInt(e, 0, 0);
    if (const DictEntry* e = d.find(21)) nominalWidthX = d.asInt(e, 0, 0);
    if (const DictEntry* e = d.find(22)) vsindex = d.asInt(e, 0, 0);
}

// ---------------------------------------------------------------------------
// CffFont parse
// ---------------------------------------------------------------------------

void CffFont::parse(Reader r) {
    buf_ = r.data();
    bufSize_ = r.size();

    hdrMajor_ = r.u8();
    uint8_t minor = r.u8();
    (void)minor;
    cff2_ = hdrMajor_ == 2;
    uint8_t hdrSize = r.u8();

    if (cff2_) {
        if (r.remaining() < 2) fail("CFF2: truncated header");
        uint32_t topDictLen = r.u16();
        uint32_t topDictStart = r.offset();
        if ((uint64_t)topDictStart + topDictLen > r.size()) fail("CFF2: top dict out of range");
        topDict.parse(r.subAt(topDictStart, topDictLen), true);
        r.seek(topDictStart + topDictLen);
        globalSubrs.parse(r.fromCursor(), true);
    } else {
        if (hdrSize < 4 || hdrSize > r.size()) fail("CFF: bad hdrSize");
        r.seek(hdrSize);
        nameIndex.parse(r.fromCursor(), false);
        r.seek(nameIndex.endOffset());
        topDictIndex.parse(r.fromCursor(), false);
        r.seek(topDictIndex.endOffset());
        stringIndex.parse(r.fromCursor(), false);
        r.seek(stringIndex.endOffset());
        globalSubrs.parse(r.fromCursor(), false);
        r.seek(globalSubrs.endOffset());

        if (nameIndex.count() > 0) {
            size_t len;
            const uint8_t* p = nameIndex.itemData(0, len);
            fontName.assign((const char*)p, len);
        }
        if (topDictIndex.count() > 0) {
            size_t len;
            const uint8_t* p = topDictIndex.itemData(0, len);
            topDict.parse(Reader(p, len), false);
        }
    }

    // Top DICT values
    if (const DictEntry* e = topDict.find2(12, 7)) {
        for (int i = 0; i < 6 && i < (int)e->operands.size(); i++)
            topMatrix[i] = topDict.asNum(e, i, topMatrix[i]);
        hasTopMatrix = true;
    }
    if (const DictEntry* e = topDict.find2(12, 30)) { // ROS
        cidRegistrySid = topDict.asInt(e, 0, -1);
        cidOrderingSid = topDict.asInt(e, 1, -1);
        cidSupplement = topDict.asInt(e, 2, 0);
        isCID = true;
    }
    if (const DictEntry* e = topDict.find(15)) charsetOffset = topDict.asInt(e, 0, 0);
    if (const DictEntry* e = topDict.find(16)) encodingOffset = topDict.asInt(e, 0, 0);
    if (const DictEntry* e = topDict.find(17)) charStringsOffset = topDict.asInt(e, 0, 0);
    if (const DictEntry* e = topDict.find(18)) {
        privSize = topDict.asInt(e, 0, 0);
        privOffset = topDict.asInt(e, 1, 0);
    }
    if (const DictEntry* e = topDict.find2(12, 36)) fdArrayOffset = topDict.asInt(e, 0, -1);
    if (const DictEntry* e = topDict.find2(12, 37)) fdSelectOffset = topDict.asInt(e, 0, -1);
    if (const DictEntry* e = topDict.find(24)) varStoreOffset = topDict.asInt(e, 0, -1);

    if (charStringsOffset <= 0 || (uint64_t)charStringsOffset >= r.size())
        fail("CFF: missing CharStrings");
    r.seek((size_t)charStringsOffset);
    charStrings.parse(r.fromCursor(), cff2_);

    parseCharset(r);

    // fonts (FDArray for CID/CFF2, else single from TopDICT)
    if (fdArrayOffset > 0 && (uint64_t)fdArrayOffset < r.size()) {
        r.seek((size_t)fdArrayOffset);
        fdArrayIndex.parse(r.fromCursor(), cff2_);
        size_t n = fdArrayIndex.count();
        for (size_t i = 0; i < n; i++) {
            CffFontDict fd;
            size_t len;
            const uint8_t* p = fdArrayIndex.itemData(i, len);
            fd.dict.parse(Reader(p, len), cff2_);
            if (const DictEntry* e = fd.dict.find2(12, 7)) {
                for (int k = 0; k < 6 && k < (int)e->operands.size(); k++)
                    fd.matrix[k] = fd.dict.asNum(e, k, fd.matrix[k]);
                fd.hasMatrix = true;
            }
            if (const DictEntry* e = fd.dict.find(18)) {
                fd.privSize = fd.dict.asInt(e, 0, 0);
                fd.privOffset = fd.dict.asInt(e, 1, 0);
            }
            fonts.push_back(std::move(fd));
        }
    } else {
        CffFontDict fd;
        fd.privOffset = privOffset;
        fd.privSize = privSize;
        fonts.push_back(std::move(fd));
    }

    for (auto& fd : fonts) {
        if (fd.privSize > 0 &&
            (uint64_t)fd.privOffset + fd.privSize <= r.size()) {
            Reader pr(r.subAt((uint32_t)fd.privOffset, (uint32_t)fd.privSize));
            fd.priv.parse(pr, cff2_);
            fd.privData = r.data() + fd.privOffset;
            fd.privDataSize = fd.privSize;
        }
    }

    // FDSelect
    hasFDSelect = fdSelectOffset > 0 && (uint64_t)fdSelectOffset < r.size();
    if (hasFDSelect) {
        Reader sr = r.subAt((uint32_t)fdSelectOffset, r.size() - (uint32_t)fdSelectOffset);
        uint8_t fmt = sr.u8();
        uint16_t ng = numGlyphs();
        if (fmt == 0) {
            for (uint16_t i = 0; i < ng && sr.remaining() >= 1; i++)
                fdSelect0.push_back(sr.u8());
        } else if (fmt == 3) {
            uint16_t nRanges = sr.u16();
            for (uint16_t i = 0; i < nRanges && sr.remaining() >= 3; i++) {
                uint16_t first = sr.u16();
                uint8_t fdIdx = sr.u8();
                fdSelect3.emplace_back(first, fdIdx);
            }
        }
    }
}

void CffFont::parseCharset(Reader r) {
    uint16_t ng = numGlyphs();
    int64_t off = charsetOffset;
    if (off == 0) { // ISOAdobe: SID = gid
        charsetSIDs.resize(ng);
        for (uint16_t i = 0; i < ng; i++) charsetSIDs[i] = i;
        charsetFormat = -2;
        return;
    }
    if (off == 1) {
        size_t n = std::min<size_t>(ng, sizeof(kExpertCharset) / sizeof(kExpertCharset[0]));
        charsetSIDs.assign(kExpertCharset, kExpertCharset + n);
        charsetSIDs.resize(ng, 0);
        charsetFormat = -3;
        return;
    }
    if (off == 2) {
        size_t n = std::min<size_t>(ng, sizeof(kExpertSubsetCharset) / sizeof(kExpertSubsetCharset[0]));
        charsetSIDs.assign(kExpertSubsetCharset, kExpertSubsetCharset + n);
        charsetSIDs.resize(ng, 0);
        charsetFormat = -4;
        return;
    }
    if ((uint64_t)off >= r.size()) {
        charsetSIDs.resize(ng, 0);
        return;
    }
    customCharset = true;
    Reader cr = r.subAt((uint32_t)off, r.size() - (uint32_t)off);
    uint8_t fmt = cr.u8();
    charsetFormat = fmt;
    charsetSIDs.clear();
    charsetSIDs.reserve(ng);
    charsetSIDs.push_back(0); // .notdef
    if (fmt == 0) {
        while (charsetSIDs.size() < ng && cr.remaining() >= 2)
            charsetSIDs.push_back(cr.u16());
    } else if (fmt == 1 || fmt == 2) {
        while (charsetSIDs.size() < ng) {
            if (fmt == 1 && cr.remaining() < 3) break;
            if (fmt == 2 && cr.remaining() < 4) break;
            uint32_t first = cr.u16();
            uint32_t nLeft = fmt == 1 ? cr.u8() : cr.u16();
            for (uint32_t i = 0; i <= nLeft && charsetSIDs.size() < ng; i++)
                charsetSIDs.push_back(first + i);
        }
    }
    charsetSIDs.resize(ng, 0);
}

uint16_t CffFont::fdOfGlyph(uint16_t gid) const {
    if (!hasFDSelect) return 0;
    if (!fdSelect0.empty())
        return gid < fdSelect0.size() ? fdSelect0[gid] : 0;
    uint16_t fd = 0;
    for (auto& rng : fdSelect3) {
        if (rng.first <= gid) fd = rng.second;
        else break;
    }
    return fd;
}

const CffIndex* CffFont::localSubrs(size_t fdIndex) const {
    if (fdIndex >= fonts.size()) return nullptr;
    if (fdIndex >= localSubrsParsed_.size()) {
        localSubrsParsed_.resize(fonts.size(), false);
        localSubrs_.resize(fonts.size());
    }
    if (!localSubrsParsed_[fdIndex]) {
        const CffFontDict& fd = fonts[fdIndex];
        if (fd.priv.subrs >= 0 && fd.privData &&
            fd.privOffset + fd.priv.subrs < (int64_t)bufSize_) {
            uint32_t off = (uint32_t)(fd.privOffset + fd.priv.subrs);
            try {
                auto idx = std::make_unique<CffIndex>();
                Reader lr(buf_, bufSize_);
                lr.seek(off);
                idx->parse(lr.fromCursor(), cff2_);
                localSubrs_[fdIndex] = std::move(idx);
            } catch (...) {
            }
        }
        localSubrsParsed_[fdIndex] = true;
    }
    return localSubrs_[fdIndex].get();
}

std::string CffFont::sidString(uint32_t sid) const {
    if (const char* s = cffStandardString(sid)) return s;
    uint32_t custom = sid - (uint32_t)cffStandardStringCount();
    if (custom < stringIndex.count()) {
        size_t len;
        const uint8_t* p = stringIndex.itemData(custom, len);
        return std::string((const char*)p, len);
    }
    return "SID" + std::to_string(sid);
}

uint32_t CffFont::glyphSID(uint16_t gid) const {
    if (gid >= charsetSIDs.size()) return 0;
    return charsetSIDs[gid];
}

std::string CffFont::glyphName(uint16_t gid) const {
    if (cff2_) return "gid" + std::to_string(gid);
    return sidString(glyphSID(gid));
}

const uint8_t* CffFont::charstringBytes(uint16_t gid, size_t& len) const {
    return charStrings.itemData(gid, len);
}

// ---------------------------------------------------------------------------
// Type2 charstring interpreter
// ---------------------------------------------------------------------------

namespace {

enum class ExecResult { Ok, Returned, Error };

struct Interp {
    const CffFont& cff;
    double cx = 0, cy = 0;
    bool sawMove = false;
    bool gotWidth = false;
    double width = 0;
    int hintCount = 0;
    int subrDepth = 0;
    double nominalWidthX = 0, defaultWidthX = 0;
    BlendRegions* regions = nullptr;
    uint16_t vsindex = 0;
    bool bad = false; // soft failure

    static constexpr size_t kMaxStack = 513;
    double stack[kMaxStack];
    size_t sp = 0;
    double transients[48] = {0};

    Interp(const CffFont& f, double nom, double defw, BlendRegions* prov)
        : cff(f), nominalWidthX(nom), defaultWidthX(defw), regions(prov) {
        memset(stack, 0, sizeof(stack));
    }

    void push(double v) {
        if (sp < kMaxStack) stack[sp++] = v;
        else bad = true;
    }
    double pop() {
        if (sp == 0) { bad = true; return 0; }
        return stack[--sp];
    }

    // Take all operands; consume a leading width if this is the first
    // stack-clearing op. oddWidth==true for h/vmoveto (width when even count),
    // false for rmoveto/endchar/hints (width when odd count).
    std::vector<double> popAllWidth(bool oddWidth) {
        std::vector<double> args(stack, stack + sp);
        sp = 0;
        if (!gotWidth) {
            gotWidth = true;
            if (cff.isCFF2()) {
                // CFF2 charstrings carry no width operand
                width = 0;
                return args;
            }
            bool present = oddWidth ? (args.size() % 2 == 0)
                                    : (args.size() % 2 == 1);
            if (present && !args.empty()) {
                width = nominalWidthX + args[0];
                args.erase(args.begin());
            } else {
                width = defaultWidthX;
            }
        }
        return args;
    }
    std::vector<double> popAll() {
        std::vector<double> args(stack, stack + sp);
        sp = 0;
        return args;
    }

    void rMoveTo(double dx, double dy) {
        closeContour();
        cx += dx;
        cy += dy;
        path.moveTo((float)cx, (float)cy);
        sawMove = true;
    }
    void rLineTo(double dx, double dy) {
        if (!sawMove) rMoveTo(0, 0);
        cx += dx;
        cy += dy;
        path.lineTo((float)cx, (float)cy);
    }
    void rCurveTo(double dx1, double dy1, double dx2, double dy2, double dx3, double dy3) {
        if (!sawMove) rMoveTo(0, 0);
        path.cubicTo((float)(cx + dx1), (float)(cy + dy1),
                     (float)(cx + dx1 + dx2), (float)(cy + dy1 + dy2),
                     (float)(cx + dx1 + dx2 + dx3), (float)(cy + dy1 + dy2 + dy3));
        cx += dx1 + dx2 + dx3;
        cy += dy1 + dy2 + dy3;
    }
    void closeContour() {
        if (sawMove) {
            path.close();
            sawMove = false;
        }
    }

    int subrBias(size_t count) const {
        if (count < 1240) return 107;
        if (count < 33900) return 1131;
        return 32768;
    }

    void run(const uint8_t* cs, size_t len, const CffIndex& localSubrs);

    ExecResult executeOp(const uint8_t*& ip, const uint8_t* end,
                         const CffIndex& localSubrs, uint8_t op, bool escaped);

    Path path;
    std::vector<SeacComponent> seac;
};

void Interp::run(const uint8_t* cs, size_t len, const CffIndex& localSubrs) {
    const uint8_t* ip = cs;
    const uint8_t* end = cs + len;
    while (ip < end && !bad) {
        uint8_t b0 = *ip++;
        if (b0 == 28) {
            if (end - ip < 2) { bad = true; break; }
            push((int16_t)((ip[0] << 8) | ip[1]));
            ip += 2;
        } else if (b0 == 29 && cff.isCFF2()) {
            if (end - ip < 4) { bad = true; break; }
            push((int32_t)(((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
                           ((uint32_t)ip[2] << 8) | ip[3]));
            ip += 4;
        } else if (b0 == 255) {
            if (end - ip < 4) { bad = true; break; }
            push((int32_t)(((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
                           ((uint32_t)ip[2] << 8) | ip[3]) / 65536.0);
            ip += 4;
        } else if (b0 >= 32 && b0 <= 246) {
            push((int32_t)b0 - 139);
        } else if (b0 >= 247 && b0 <= 250) {
            if (end - ip < 1) { bad = true; break; }
            push((b0 - 247) * 256 + *ip++ + 108);
        } else if (b0 >= 251 && b0 <= 254) {
            if (end - ip < 1) { bad = true; break; }
            push(-(b0 - 251) * 256 - *ip++ - 108);
        } else if (b0 == 12) {
            if (end - ip < 1) { bad = true; break; }
            uint8_t op2 = *ip++;
            if (executeOp(ip, end, localSubrs, op2, true) != ExecResult::Ok)
                break;
        } else {
            if (executeOp(ip, end, localSubrs, b0, false) != ExecResult::Ok)
                break;
        }
    }
    // Only the top-level charstring closes the final contour; a subroutine
    // returning to its caller must leave the path open (fontTools subrLevel).
    if (subrDepth == 0) closeContour();
}

ExecResult Interp::executeOp(const uint8_t*& ip, const uint8_t* end,
                             const CffIndex& localSubrs, uint8_t op, bool escaped) {
    if (!escaped) {
        switch (op) {
        case 1: case 3: case 18: case 23: { // stems
            auto args = popAllWidth(false);
            hintCount += (int)(args.size() / 2);
            break;
        }
        case 19: case 20: { // hintmask / cntrmask
            auto args = popAllWidth(false);
            hintCount += (int)(args.size() / 2);
            size_t nBytes = ((size_t)hintCount + 7) / 8;
            if ((size_t)(end - ip) < nBytes) return ExecResult::Error;
            ip += nBytes;
            break;
        }
        case 21: { // rmoveto
            auto args = popAllWidth(false);
            if (args.size() < 2) return ExecResult::Error;
            rMoveTo(args[args.size() - 2], args[args.size() - 1]);
            break;
        }
        case 22: { // hmoveto
            auto args = popAllWidth(true);
            if (args.empty()) return ExecResult::Error;
            rMoveTo(args.back(), 0);
            break;
        }
        case 4: { // vmoveto
            auto args = popAllWidth(true);
            if (args.empty()) return ExecResult::Error;
            rMoveTo(0, args.back());
            break;
        }
        case 5: { // rlineto
            auto args = popAll();
            for (size_t i = 0; i + 1 < args.size(); i += 2)
                rLineTo(args[i], args[i + 1]);
            break;
        }
        case 6: { // hlineto
            auto args = popAll();
            bool horiz = true;
            for (double a : args) {
                if (horiz) rLineTo(a, 0);
                else rLineTo(0, a);
                horiz = !horiz;
            }
            break;
        }
        case 7: { // vlineto
            auto args = popAll();
            bool horiz = false;
            for (double a : args) {
                if (horiz) rLineTo(a, 0);
                else rLineTo(0, a);
                horiz = !horiz;
            }
            break;
        }
        case 8: { // rrcurveto
            auto args = popAll();
            for (size_t i = 0; i + 5 < args.size(); i += 6)
                rCurveTo(args[i], args[i + 1], args[i + 2], args[i + 3], args[i + 4], args[i + 5]);
            break;
        }
        case 24: { // rcurveline
            auto args = popAll();
            if (args.size() < 8) return ExecResult::Error;
            size_t nCurves = (args.size() - 2) / 6;
            size_t used = 0;
            for (size_t c = 0; c < nCurves; c++, used += 6)
                rCurveTo(args[used], args[used + 1], args[used + 2],
                         args[used + 3], args[used + 4], args[used + 5]);
            rLineTo(args[args.size() - 2], args[args.size() - 1]);
            break;
        }
        case 25: { // rlinecurve
            auto args = popAll();
            if (args.size() < 8) return ExecResult::Error;
            size_t nLines = (args.size() - 6) / 2;
            size_t used = 0;
            for (size_t l = 0; l < nLines; l++, used += 2)
                rLineTo(args[used], args[used + 1]);
            rCurveTo(args[used], args[used + 1], args[used + 2],
                     args[used + 3], args[used + 4], args[used + 5]);
            break;
        }
        case 26: { // vvcurveto
            auto args = popAll();
            double dx1 = 0;
            size_t i = 0;
            if (args.size() % 2 == 1) dx1 = args[i++];
            for (; i + 4 <= args.size(); i += 4) {
                double dya = args[i], dxb = args[i + 1], dyb = args[i + 2], dyc = args[i + 3];
                rCurveTo(dx1, dya, dxb, dyb, 0, dyc);
                dx1 = 0;
            }
            break;
        }
        case 27: { // hhcurveto
            auto args = popAll();
            double dy1 = 0;
            size_t i = 0;
            if (args.size() % 2 == 1) dy1 = args[i++];
            for (; i + 4 <= args.size(); i += 4) {
                double dxa = args[i], dxb = args[i + 1], dyb = args[i + 2], dxc = args[i + 3];
                rCurveTo(dxa, dy1, dxb, dyb, dxc, 0);
                dy1 = 0;
            }
            break;
        }
        case 30:   // vhcurveto
        case 31: { // hvcurveto
            auto args = popAll();
            bool verticalFirst = (op == 30);
            size_t i = 0;
            while (i + 4 <= args.size()) {
                double a0 = args[i], a1 = args[i + 1], a2 = args[i + 2], a3 = args[i + 3];
                i += 4;
                double extra = 0;
                bool haveExtra = false;
                if (args.size() - i == 1) {
                    extra = args[i];
                    i++;
                    haveExtra = true;
                }
                if (verticalFirst)
                    rCurveTo(0, a0, a1, a2, a3, haveExtra ? extra : 0);
                else
                    rCurveTo(a0, 0, a1, a2, haveExtra ? extra : 0, a3);
                verticalFirst = !verticalFirst;
                if (haveExtra) break;
            }
            break;
        }
        case 10: { // callsubr
            if (sp == 0) return ExecResult::Error;
            {
                int idx = (int)pop() + subrBias(localSubrs.count());
                if (idx < 0 || (size_t)idx >= localSubrs.count()) return ExecResult::Error;
                if (++subrDepth > 32) return ExecResult::Error;
                size_t len;
                const uint8_t* p = localSubrs.itemData((size_t)idx, len);
                run(p, len, localSubrs);
                subrDepth--;
            }
            break;
        }
        case 29: { // callgsubr
            if (sp == 0) return ExecResult::Error;
            {
                int idx = (int)pop() + subrBias(cff.globalSubrs.count());
                if (idx < 0 || (size_t)idx >= cff.globalSubrs.count()) return ExecResult::Error;
                if (++subrDepth > 32) return ExecResult::Error;
                size_t len;
                const uint8_t* p = cff.globalSubrs.itemData((size_t)idx, len);
                run(p, len, localSubrs);
                subrDepth--;
            }
            break;
        }
        case 11: // return
            return ExecResult::Returned;
        case 15: { // vsindex (CFF2; plain operator)
            auto args = popAll();
            if (!args.empty()) vsindex = (uint16_t)std::lround(args.back());
            break;
        }
        case 16: { // blend (CFF2; plain operator)
            if (sp == 0) return ExecResult::Error;
            int n = (int)pop();
            if (n < 0 || (size_t)n > sp) return ExecResult::Error;
            size_t nRegions = 0;
            const std::vector<float>* scalars = nullptr;
            if (regions) {
                scalars = &regions->scalars(vsindex);
                nRegions = scalars->size();
            }
            if ((size_t)n * (1 + nRegions) > sp)
                nRegions = n > 0 ? (sp - (size_t)n) / (size_t)n : 0;
            size_t total = (size_t)n * (1 + nRegions);
            if (total > sp) {
                nRegions = n > 0 ? (sp - (size_t)n) / (size_t)n : 0;
                total = (size_t)n * (1 + nRegions);
            }
            std::vector<double> base(stack + sp - total, stack + sp - (size_t)n * nRegions);
            std::vector<double> deltas(stack + sp - (size_t)n * nRegions, stack + sp);
            sp -= total;
            for (int i = 0; i < n; i++) {
                double v = (size_t)i < base.size() ? base[i] : 0;
                for (size_t r = 0; r < nRegions; r++)
                    v += deltas[r * (size_t)n + i] * (scalars ? (*scalars)[r] : 0.0);
                push(v);
            }
            break;
        }
        case 14: { // endchar
            auto args = popAllWidth(false);
            if (!cff.isCFF2() && args.size() >= 4) {
                SeacComponent base;
                base.glyphName = standardEncodingName((uint8_t)args[2]);
                SeacComponent accent;
                accent.glyphName = standardEncodingName((uint8_t)args[3]);
                accent.dx = args[0];
                accent.dy = args[1];
                seac.push_back(base);
                seac.push_back(accent);
            }
            closeContour();
            return ExecResult::Returned;
        }
        default:
            break;
        }
    } else {
        switch (op) {
        case 34: { // hflex
            auto args = popAll();
            if (args.size() < 7) return ExecResult::Error;
            double dx1 = args[0], dx2 = args[1], dy2 = args[2], dx3 = args[3],
                   dx4 = args[4], dx5 = args[5], dx6 = args[6];
            rCurveTo(dx1, 0, dx2, dy2, dx3, 0);
            rCurveTo(dx4, 0, dx5, -dy2, dx6, 0);
            break;
        }
        case 35: { // flex
            auto args = popAll();
            if (args.size() < 13) return ExecResult::Error;
            rCurveTo(args[0], args[1], args[2], args[3], args[4], args[5]);
            rCurveTo(args[6], args[7], args[8], args[9], args[10], args[11]);
            break;
        }
        case 36: { // hflex1
            auto args = popAll();
            if (args.size() < 9) return ExecResult::Error;
            double dx1 = args[0], dy1 = args[1], dx2 = args[2], dy2 = args[3],
                   dx3 = args[4], dx4 = args[5], dx5 = args[6], dy5 = args[7], dx6 = args[8];
            double dy6 = -(dy1 + dy2 + dy5);
            rCurveTo(dx1, dy1, dx2, dy2, dx3, 0);
            rCurveTo(dx4, 0, dx5, dy5, dx6, dy6);
            break;
        }
        case 37: { // flex1
            auto args = popAll();
            if (args.size() < 11) return ExecResult::Error;
            double dx1 = args[0], dy1 = args[1], dx2 = args[2], dy2 = args[3],
                   dx3 = args[4], dy3 = args[5], dx4 = args[6], dy4 = args[7],
                   dx5 = args[8], dy5 = args[9], d6 = args[10];
            double dx = dx1 + dx2 + dx3 + dx4 + dx5;
            double dy = dy1 + dy2 + dy3 + dy4 + dy5;
            double dx6, dy6;
            if (std::fabs(dx) > std::fabs(dy)) { dx6 = d6; dy6 = -dy; }
            else { dx6 = -dx; dy6 = d6; }
            rCurveTo(dx1, dy1, dx2, dy2, dx3, dy3);
            rCurveTo(dx4, dy4, dx5, dy5, dx6, dy6);
            break;
        }
        case 15: { // (escaped 12 15 is undefined; ignore)
            auto args = popAll();
            (void)args;
            break;
        }
        case 16: { // (escaped 12 16 is undefined; ignore)
            auto args = popAll();
            (void)args;
            break;
        }
        case 10: { if (sp < 2) return ExecResult::Error; double b = pop(); push(pop() + b); break; } // add
        case 11: { if (sp < 2) return ExecResult::Error; double b = pop(); push(pop() - b); break; } // sub
        case 12: { if (sp < 2) return ExecResult::Error; double b = pop(); if (b == 0) return ExecResult::Error; push(pop() / b); break; } // div
        case 24: { if (sp < 2) return ExecResult::Error; double b = pop(); push(pop() * b); break; } // mul
        case 26: { if (sp < 1) return ExecResult::Error; push(std::sqrt(pop())); break; } // sqrt
        case 14: { if (sp < 1) return ExecResult::Error; push(-pop()); break; } // neg
        case 9:  { if (sp < 1) return ExecResult::Error; push(std::fabs(pop())); break; } // abs
        case 18: { if (sp < 1) return ExecResult::Error; pop(); break; } // drop
        case 27: { if (sp < 1) return ExecResult::Error; push(stack[sp - 1]); break; } // dup
        case 28: { if (sp < 2) return ExecResult::Error; std::swap(stack[sp - 1], stack[sp - 2]); break; } // exch
        case 29: { // index
            if (sp < 1) return ExecResult::Error;
            int idx = (int)pop();
            if (idx < 0) idx = 0;
            if ((size_t)idx < sp) push(stack[sp - 1 - (size_t)idx]);
            else push(0);
            break;
        }
        case 30: { // roll — approximate
            if (sp < 3) return ExecResult::Error;
            pop(); pop(); pop();
            break;
        }
        case 20: { // put
            if (sp < 2) return ExecResult::Error;
            int idx = (int)pop();
            double v = pop();
            if (idx >= 0 && idx < 48) transients[idx] = v;
            break;
        }
        case 21: { // get
            if (sp < 1) return ExecResult::Error;
            int idx = (int)pop();
            push(idx >= 0 && idx < 48 ? transients[idx] : 0);
            break;
        }
        default:
            break;
        }
    }
    return bad ? ExecResult::Error : ExecResult::Ok;
}

} // namespace

CffGlyphInfo CffFont::interpret(uint16_t gid, BlendRegions* provider) const {
    CffGlyphInfo info;
    size_t len = 0;
    const uint8_t* cs = nullptr;
    try {
        cs = charstringBytes(gid, len);
    } catch (const Error&) {
        info.ok = false;
        return info;
    }

    uint16_t fd = fdOfGlyph(gid);
    double nom = 0, defw = 0;
    if (fd < fonts.size()) {
        nom = fonts[fd].priv.nominalWidthX;
        defw = fonts[fd].priv.defaultWidthX;
    }
    const CffIndex* local = localSubrs(fd);
    static const CffIndex kEmpty;
    if (!local) local = &kEmpty;

    Interp interp(*this, nom, defw, provider);
    try {
        interp.run(cs, len, *local);
    } catch (const Error&) {
        interp.bad = true;
    }
    info.path = std::move(interp.path);
    info.width = interp.gotWidth ? interp.width : 0;
    info.hasWidth = interp.gotWidth;
    info.seac = std::move(interp.seac);
    info.hintCount = (size_t)interp.hintCount;
    info.ok = !interp.bad;
    return info;
}

} // namespace ftk
