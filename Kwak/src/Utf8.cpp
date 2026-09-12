// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#include "Utf8.h"

namespace utf8 {

std::u32string decode(const std::string &utf8Bytes)
{
    std::u32string out;
    out.reserve(utf8Bytes.size());

    const auto n = utf8Bytes.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char b0 = static_cast<unsigned char>(utf8Bytes[i]);
        int extraBytes = 0;
        char32_t cp = 0;

        if ((b0 & 0x80) == 0x00) {      // 0xxxxxxx -- 1-byte (ASCII)
            cp = b0;
            extraBytes = 0;
        } else if ((b0 & 0xE0) == 0xC0) { // 110xxxxx -- 2-byte
            cp = b0 & 0x1F;
            extraBytes = 1;
        } else if ((b0 & 0xF0) == 0xE0) { // 1110xxxx -- 3-byte (all of
            cp = b0 & 0x0F;                // Cyrillic lives here)
            extraBytes = 2;
        } else if ((b0 & 0xF8) == 0xF0) { // 11110xxx -- 4-byte
            cp = b0 & 0x07;
            extraBytes = 3;
        } else {
            // Not a valid UTF-8 leader byte (a stray continuation byte, or
            // a now-obsolete 5/6-byte leader) -- pass it through unchanged
            // rather than throw, so non-UTF-8 input degrades instead of
            // crashing. Matches QString::fromUtf8()'s leniency.
            out.push_back(b0);
            ++i;
            continue;
        }

        bool truncated = false;
        for (int k = 1; k <= extraBytes; ++k) {
            if (i + static_cast<std::size_t>(k) >= n) { truncated = true; break; }
            const unsigned char bk = static_cast<unsigned char>(utf8Bytes[i + static_cast<std::size_t>(k)]);
            if ((bk & 0xC0) != 0x80) { truncated = true; break; } // not a continuation byte
            cp = (cp << 6) | (bk & 0x3F);
        }

        if (truncated) {
            out.push_back(b0); // same lenient fallback as the invalid-leader case above
            ++i;
            continue;
        }

        out.push_back(cp);
        i += static_cast<std::size_t>(extraBytes) + 1;
    }
    return out;
}

std::string encode(const std::u32string &codepoints)
{
    std::string out;
    out.reserve(codepoints.size());

    for (char32_t cp : codepoints) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

char32_t toUpper(char32_t ch)
{
    // ASCII a-z -> A-Z.
    if (ch >= U'a' && ch <= U'z')
        return ch - (U'a' - U'A');
    // Cyrillic а-я (0x0430-0x044F) -> А-Я (0x0410-0x042F) -- the same +0x20
    // offset as ASCII's case pair, and the only Cyrillic range this
    // checkerboard's alphabet uses (it excludes Ё/ё and Ъ/ъ -- see
    // number2cyrTable()'s comment in OTP.cpp).
    if (ch >= 0x0430 && ch <= 0x044F)
        return ch - 0x20;
    return ch;
}

std::u32string toUpper(const std::u32string &s)
{
    std::u32string out;
    out.reserve(s.size());
    for (char32_t ch : s)
        out.push_back(toUpper(ch));
    return out;
}

} // namespace utf8
