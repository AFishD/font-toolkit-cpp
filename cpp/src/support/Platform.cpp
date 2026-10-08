#include "ftk/Platform.hpp"
#include "ftk/Bytes.hpp"

#include <algorithm>
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ftk {

// ---------------------------------------------------------------------------
// Unicode
// ---------------------------------------------------------------------------

static void pushUtf8(std::string& out, uint32_t cp) {
    if (cp > 0x10FFFF) cp = 0xFFFD;
    if (cp >= 0x10000) {
        uint32_t v = cp - 0x10000;
        out += char(0xF0 | (v >> 18));
        out += char(0x80 | ((v >> 12) & 0x3F));
        out += char(0x80 | ((v >> 6) & 0x3F));
        out += char(0x80 | (v & 0x3F));
    } else if (cp >= 0x800) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp >= 0x80) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(cp);
    }
}

std::string utf16ToUtf8(const std::vector<uint16_t>& u) {
    std::string out;
    out.reserve(u.size());
    for (size_t i = 0; i < u.size(); i++) {
        uint32_t c = u[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF) {
                uint32_t cp = 0x10000 + ((c - 0xD800) << 10) + (u[i + 1] - 0xDC00);
                pushUtf8(out, cp);
                i++;
            } else {
                pushUtf8(out, 0xFFFD);
            }
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            pushUtf8(out, 0xFFFD);
        } else {
            pushUtf8(out, c);
        }
    }
    return out;
}

std::string utf16BEToUtf8(const uint8_t* p, size_t nBytes) {
    std::vector<uint16_t> u;
    u.reserve(nBytes / 2);
    for (size_t i = 0; i + 1 < nBytes; i += 2)
        u.push_back((uint16_t)((p[i] << 8) | p[i + 1]));
    return utf16ToUtf8(u);
}

std::vector<uint8_t> utf8ToUtf16BE(const std::string& s) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = utf8DecodeOne(s, i);
        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000;
            uint16_t hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
            out.push_back(uint8_t(hi >> 8)); out.push_back(uint8_t(hi));
            out.push_back(uint8_t(lo >> 8)); out.push_back(uint8_t(lo));
        } else {
            out.push_back(uint8_t(cp >> 8)); out.push_back(uint8_t(cp));
        }
    }
    return out;
}

uint32_t utf8DecodeOne(const std::string& s, size_t& i) {
    uint8_t c = s[i++];
    if (c < 0x80) return c;
    auto cont = [&](size_t j) -> int { return j < s.size() ? (s[j] & 0xC0) == 0x80 : false; };
    if ((c & 0xE0) == 0xC0 && cont(i)) return ((c & 0x1F) << 6) | (s[i++] & 0x3F);
    if ((c & 0xF0) == 0xE0 && cont(i) && cont(i + 1)) {
        uint32_t cp = ((c & 0x0F) << 12) | ((s[i] & 0x3F) << 6) | (s[i + 1] & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && cont(i) && cont(i + 1) && cont(i + 2)) {
        uint32_t cp = ((c & 0x07) << 18) | ((s[i] & 0x3F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        i += 3;
        return cp;
    }
    return 0xFFFD;
}

bool validUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = s[i];
        if (c < 0x80) { i++; continue; }
        size_t start = i;
        uint32_t cp = utf8DecodeOne(s, i);
        if (cp == 0xFFFD && i == start + 1) return false;
    }
    return true;
}

// Mac Roman high-byte mapping (0x80-0xFF) to Unicode.
static const uint16_t kMacRoman[128] = {
    0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
    0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
    0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00AB,0x00BB,
    0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,0x00FF,
    0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,0x00CB,
    0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,0x00AF,
    0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7,0x00D7,0x2044,0x00A4,0x2039,0x203A,0xFB01,0xFB02,0x00BB,0x00AB,
    // 0xF0-0xF5 are unmapped (corporate); map to replacement
    0x2026,0x2026,0x2026,0x2026,0x2026,0x2026,0x00AF,0x02DB,0x02C7,0x00D7,0x00A4,0x2039,0x203A,0xFB01,0xFB02,0x00AB
};

std::string macRomanToUtf8(const uint8_t* p, size_t n) {
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (c < 0x80) out += char(c);
        else pushUtf8(out, kMacRoman[c - 0x80]);
    }
    return out;
}

std::string windows1252ToUtf8(const uint8_t* p, size_t n) {
    static const uint16_t map[32] = {
        0x20AC,0xFFFD,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0xFFFD,0x017D,0xFFFD,
        0xFFFD,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0xFFFD,0x017E,0x0178
    };
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (c < 0x80) out += char(c);
        else if (c >= 0xA0) pushUtf8(out, c);
        else pushUtf8(out, map[c - 0x80]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------

#ifdef _WIN32

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::vector<uint8_t> readFile(const std::string& utf8Path) {
    HANDLE h = CreateFileW(utf8ToWide(utf8Path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) fail("cannot open file: " + utf8Path + " (err " + std::to_string(GetLastError()) + ")");
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); fail("cannot size file: " + utf8Path); }
    std::vector<uint8_t> buf((size_t)sz.QuadPart);
    size_t got = 0;
    while (got < buf.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(buf.size() - got, 1u << 30);
        DWORD rd = 0;
        if (!ReadFile(h, buf.data() + got, chunk, &rd, nullptr) || rd == 0) {
            CloseHandle(h);
            fail("read failed: " + utf8Path);
        }
        got += rd;
    }
    CloseHandle(h);
    return buf;
}

bool writeFile(const std::string& utf8Path, const void* p, size_t n) {
    HANDLE h = CreateFileW(utf8ToWide(utf8Path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t put = 0;
    while (put < n) {
        DWORD chunk = (DWORD)std::min<size_t>(n - put, 1u << 30);
        DWORD wr = 0;
        if (!WriteFile(h, (const uint8_t*)p + put, chunk, &wr, nullptr) || wr != chunk) {
            CloseHandle(h);
            return false;
        }
        put += wr;
    }
    CloseHandle(h);
    return true;
}

bool fileExists(const std::string& utf8Path) {
    DWORD a = GetFileAttributesW(utf8ToWide(utf8Path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<std::string> listFiles(const std::string& utf8Dir, bool recursive) {
    std::vector<std::string> out;
    std::wstring dirW = utf8ToWide(utf8Dir);
    while (!dirW.empty() && (dirW.back() == L'/' || dirW.back() == L'\\')) dirW.pop_back();
    std::wstring pattern = dirW + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::string full = wideToUtf8(dirW + L"\\" + name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) {
                auto sub = listFiles(full, true);
                out.insert(out.end(), sub.begin(), sub.end());
            }
        } else {
            out.push_back(full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

uint64_t fileMTime(const std::string& utf8Path) {
    HANDLE h = CreateFileW(utf8ToWide(utf8Path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FILETIME ft;
    GetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

uint64_t fileSizeOf(const std::string& utf8Path) {
    HANDLE h = CreateFileW(utf8ToWide(utf8Path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    CloseHandle(h);
    return (uint64_t)sz.QuadPart;
}

#else // POSIX

std::vector<uint8_t> readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) fail("cannot open file: " + path);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(sz);
    if (sz && fread(buf.data(), 1, sz, f) != (size_t)sz) { fclose(f); fail("read failed: " + path); }
    fclose(f);
    return buf;
}

bool writeFile(const std::string& path, const void* p, size_t n) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = n == 0 || fwrite(p, 1, n, f) == n;
    fclose(f);
    return ok;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<std::string> listFiles(const std::string& dir, bool recursive) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (recursive) {
                    auto sub = listFiles(full, true);
                    out.insert(out.end(), sub.begin(), sub.end());
                }
            } else {
                out.push_back(full);
            }
        }
    }
    closedir(d);
    return out;
}

uint64_t fileMTime(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return (uint64_t)st.st_mtime;
    return 0;
}

uint64_t fileSizeOf(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return (uint64_t)st.st_size;
    return 0;
}

#endif

std::string baseName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string dirName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? std::string() : path.substr(0, p);
}

std::string toLowerAscii(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

#ifdef _WIN32

std::vector<std::string> utf8CommandLineArgs(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::vector<std::string> out;
    int n = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!wide) {
        for (int i = 0; i < argc; i++) out.push_back(argv[i]);
        return out;
    }
    for (int i = 0; i < n; i++)
        out.push_back(wideToUtf8(wide[i]));
    LocalFree(wide);
    return out;
}

void enableUtf8Console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

#else

std::vector<std::string> utf8CommandLineArgs(int argc, char** argv) {
    std::vector<std::string> out;
    for (int i = 0; i < argc; i++) out.push_back(argv[i]);
    return out;
}

void enableUtf8Console() {}

#endif

} // namespace ftk
