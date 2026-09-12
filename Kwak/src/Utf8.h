// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#pragma once

// Utf8 -- the one piece of plumbing Qt gave Quacque for free that standard
// C++ doesn't: converting between on-disk/on-the-wire UTF-8 bytes
// (std::string) and in-memory Unicode codepoints (std::u32string) for
// per-character processing (the straddling checkerboard, Morse-cut
// substitution, the TerminalEditor buffer, ...).
//
// Why char32_t/std::u32string rather than wchar_t/std::wstring: wchar_t is
// 16 bits on Windows and 32 bits on Linux/macOS, so code written against it
// silently does the wrong thing on one platform or the other for anything
// outside the Latin-1 range -- exactly the gap flagged (but left unfixed,
// since it only ever had to run on Windows) in Quacque's original
// TerminalEditor.cpp. char32_t is a fixed 32-bit type everywhere, so one
// codepoint is always exactly one char32_t on every platform this targets --
// the single biggest reason Kwak can use identical source on Windows and
// Linux with no platform-conditional *logic*, only a different curses
// backend selected at build time. See kwak_design.md.
//
// Every character this app's straddling checkerboard actually uses (Roman
// and Cyrillic letters, digits, punctuation) is in the Basic Multilingual
// Plane, so "one QChar per character" in the original Qt code and "one
// char32_t per character" here are exactly equivalent -- no surrogate pairs,
// no combining characters, nothing subtle lost in translation.

#include <cstdint>
#include <string>

namespace utf8 {

// Decodes a UTF-8 byte string into one char32_t per Unicode codepoint.
// Malformed input (a truncated multi-byte sequence, an overlong encoding, a
// continuation byte with no leader) is not expected from this app's own
// output, but is handled defensively: an invalid byte is passed through
// as-is (so round-tripping genuinely non-UTF-8 garbage doesn't throw or
// silently drop data), matching the spirit of Qt's own lenient
// QString::fromUtf8().
std::u32string decode(const std::string &utf8Bytes);

// Inverse of decode(): encodes one char32_t per codepoint back into UTF-8
// bytes.
std::string encode(const std::u32string &codepoints);

// Uppercases a single codepoint -- covers exactly the ranges this app's
// checkerboard alphabet needs (ASCII A-Z and the Cyrillic block used by
// validCyrillicStr/number2cyrTable), not general Unicode case-folding. Used
// in place of QChar::toUpper()/QString::toUpper() at the few call sites that
// need it (encode()'s toUpper() normalization, TerminalEditor's
// case-insensitive allowedChars match).
char32_t toUpper(char32_t ch);
std::u32string toUpper(const std::u32string &s);

} // namespace utf8
