// ftk: unicode + filesystem helpers (UTF-8 paths on all platforms).
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ftk {

// UTF-16BE bytes -> UTF-8 string.
std::string utf16BEToUtf8(const uint8_t* p, size_t nBytes);
// UTF-8 -> UTF-16BE bytes.
std::vector<uint8_t> utf8ToUtf16BE(const std::string& s);
// UTF-16 (host endian pairs in uint16 vector) -> UTF-8.
std::string utf16ToUtf8(const std::vector<uint16_t>& u);

bool validUtf8(const std::string& s);
// Decode one codepoint at byte offset i; advances i. Returns U+FFFD on error.
uint32_t utf8DecodeOne(const std::string& s, size_t& i);

// Windows-1252 (approximation of mac_roman for latin range is separate below).
std::string macRomanToUtf8(const uint8_t* p, size_t n);
std::string windows1252ToUtf8(const uint8_t* p, size_t n);

// ---- filesystem ----
std::vector<uint8_t> readFile(const std::string& utf8Path);          // throws ftk::Error
bool writeFile(const std::string& utf8Path, const void* p, size_t n);
bool fileExists(const std::string& utf8Path);
std::vector<std::string> listFiles(const std::string& utf8Dir, bool recursive);
std::string baseName(const std::string& path);
std::string dirName(const std::string& path);
std::string toLowerAscii(std::string s);
std::string joinPath(const std::string& a, const std::string& b);

uint64_t fileMTime(const std::string& utf8Path);
uint64_t fileSizeOf(const std::string& utf8Path);

// Re-reads the process command line as UTF-8. On Windows the CRT hands us
// ANSI-encoded argv, which corrupts non-ASCII font paths; this uses the wide
// command line instead.
std::vector<std::string> utf8CommandLineArgs(int argc, char** argv);
// Best-effort: make console output UTF-8 on Windows.
void enableUtf8Console();

} // namespace ftk
