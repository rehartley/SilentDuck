// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#include "OTP.h"
#include "SecureRandom.h"
#include "Utf8.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

// Force an OS-level flush of f's buffered writes to physical storage -- the
// libc-level fflush() alone only moves data from the app's buffers to the
// OS's, not from the OS's page cache to the device. Used after every wipe
// pass in wipeFile() below and after writeFile()'s single write. Ported
// unchanged from Quacque's OTP.cpp (it was already Qt-free, plain WinAPI/
// POSIX conditional on a FILE* rather than a QFile handle).
static void forceFlushToDisk(std::FILE *f)
{
#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(f)));
    if (h != INVALID_HANDLE_VALUE)
        FlushFileBuffers(h);
#else
    fsync(fileno(f));
#endif
}

namespace {

// Replacement for QString's ".arg(value, width, 10, QChar('0'))" -- the
// handful of zero-padded fixed-width numbers this file formats into
// filenames and checksums (".otk" page numbers, 5-digit checksums, ...).
std::string zeroPad(int value, int width)
{
    std::string s = std::to_string(value);
    if (static_cast<int>(s.size()) < width)
        s = std::string(static_cast<std::size_t>(width) - s.size(), '0') + s;
    return s;
}

std::string join(const std::vector<std::string> &parts, const std::string &sep)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += sep;
        out += parts[i];
    }
    return out;
}

} // namespace

OTP::OTP()
{
    buildTables();
}

// ============================================================================
// straddling-checkerboard tables
// ============================================================================

namespace {

// number -> Roman letter (single digits, then double digits 70-99).
const std::map<std::string, char32_t> &number2latTable()
{
    static const std::map<std::string, char32_t> tbl = {
        {"0", U'A'}, {"1", U'E'}, {"2", U'N'}, {"3", U'R'},
        {"4", U'O'}, {"5", U'I'}, {"6", U'T'},
        {"70", U'B'}, {"71", U'C'}, {"72", U'G'}, {"73", U'D'}, {"74", U'F'},
        {"75", U'H'}, {"76", U'J'}, {"77", U'K'}, {"78", U'L'}, {"79", U'M'},
        {"80", U'P'}, {"81", U'Q'}, {"82", U'S'}, {"83", U'U'}, {"84", U'V'},
        {"85", U'W'}, {"86", U'X'}, {"87", U'Y'}, {"88", U'Z'},
        // 89 intentionally unassigned -- see otp.py's comment: reserved for a
        // raw-code-plus-length scheme that was never implemented.
        {"90", U'?'}, {"91", U':'}, {"92", U'@'}, {"93", U'/'},
        {"94", U'#'}, {"95", U'.'}, {"96", U','}, {"97", U'\n'},
        {"98", U' '}, {"99", U'~'},
    };
    return tbl;
}

// number -> Cyrillic letter. Built from numeric char32_t codepoints (not
// embedded literal Cyrillic bytes) for the same file-corruption-resistance
// reason as Quacque's OTP.cpp -- see this file's top comment and OTP.h.
// А=0x0410 Б=0x0411 В=0x0412 Г=0x0413 Д=0x0414 Е=0x0415 Ж=0x0416 З=0x0417
// И=0x0418 Й=0x0419 К=0x041A Л=0x041B М=0x041C Н=0x041D О=0x041E П=0x041F
// Р=0x0420 С=0x0421 Т=0x0422 У=0x0423 Ф=0x0424 Х=0x0425 Ц=0x0426 Ч=0x0427
// Ш=0x0428 Щ=0x0429 Ы=0x042B Ь=0x042C Э=0x042D Ю=0x042E Я=0x042F
const std::map<std::string, char32_t> &number2cyrTable()
{
    static const std::map<std::string, char32_t> tbl = {
        {"0", 0x0410}, {"1", 0x0415}, {"2", 0x0418}, {"3", 0x041D},
        {"4", 0x041E}, {"5", 0x0421}, {"6", 0x0422},
        {"70", 0x0411}, {"71", 0x0412}, {"72", 0x0413}, {"73", 0x0414}, {"74", 0x0416},
        {"75", 0x0417}, {"76", 0x0419}, {"77", 0x041A}, {"78", 0x041B}, {"79", 0x041C},
        {"80", 0x041F}, {"81", 0x0420}, {"82", 0x0423}, {"83", 0x0424}, {"84", 0x0425},
        {"85", 0x0426}, {"86", 0x0427}, {"87", 0x0428}, {"88", 0x0429}, {"89", 0x042B},
        {"90", 0x042C}, {"91", 0x042D}, {"92", 0x042E}, {"93", 0x042F},
        {"94", U'#'}, {"95", U'.'}, {"96", U','}, {"97", U'\n'},
        {"98", U' '}, {"99", U'~'},
    };
    return tbl;
}

} // namespace

void OTP::buildTables()
{
    m_number2lat = number2latTable();
    m_number2cyr = number2cyrTable();

    m_lat2number.clear();
    for (const auto &kv : m_number2lat)
        m_lat2number[kv.second] = kv.first;

    m_cyr2number.clear();
    for (const auto &kv : m_number2cyr)
        m_cyr2number[kv.second] = kv.first;
}

std::u32string OTP::allowedInputChars()
{
    static const std::u32string validRomanStr  = U"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const std::u32string validPunctStr  = U"?:@/~#., \n";
    static const std::u32string validDigitsStr = U"0123456789";

    // Natural Cyrillic reading order (А Б В Г Д Е Ж З И...), since this is
    // meant to be read/pasted as a cheat-sheet -- deliberately NOT the
    // frequency-coded order number2cyrTable() above uses. Same 31 letters
    // either way (the Russian alphabet minus Ё and Ъ).
    static const std::u32string validCyrillicStr = [] {
        static const char32_t cyrillic[] = {
            0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
            0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
            0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
            0x0428, 0x0429, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
        };
        std::u32string s;
        for (char32_t cp : cyrillic)
            s.push_back(cp);
        return s;
    }();

    static const std::u32string validStr = validRomanStr + validPunctStr + validDigitsStr + validCyrillicStr;
    return validStr;
}

bool OTP::isAllowedInputChar(char32_t ch)
{
    static const std::u32string chars = allowedInputChars();
    return chars.find(utf8::toUpper(ch)) != std::u32string::npos;
}

bool OTP::stringValid(const std::u32string &s) const
{
    for (char32_t ch : s) {
        if (!isAllowedInputChar(ch))
            return false;
    }
    return true;
}

// ============================================================================
// digit-string arithmetic
// ============================================================================

std::string OTP::stringAddDigits(const std::string &a, const std::string &b, bool *ok)
{
    if (ok) *ok = true;
    if (a.length() > b.length()) {
        setError("stringAdd: len(a) > len(b) by " + std::to_string(a.length() - b.length()) + " characters");
        if (ok) *ok = false;
        return std::string();
    }
    std::string tmp;
    tmp.reserve(a.length());
    for (std::size_t x = 0; x < a.length(); ++x) {
        int d = (a[x] - '0') + (b[x] - '0');
        if (d > 9) d -= 10;
        tmp += static_cast<char>('0' + d);
    }
    return tmp;
}

std::string OTP::stringSubtractDigits(const std::string &a, const std::string &b, bool *ok)
{
    if (ok) *ok = true;
    if (a.length() > b.length()) {
        setError("stringSubtract: len(a) > len(b) by " + std::to_string(a.length() - b.length()) + " characters");
        if (ok) *ok = false;
        return std::string();
    }
    std::string tmp;
    tmp.reserve(a.length());
    for (std::size_t x = 0; x < a.length(); ++x) {
        int d = (a[x] - '0') - (b[x] - '0');
        if (d < 0) d += 10;
        tmp += static_cast<char>('0' + d);
    }
    return tmp;
}

std::string OTP::stringDigits(const std::string &s)
{
    std::string tmp;
    tmp.reserve(s.length());
    for (char ch : s) {
        if (ch >= '0' && ch <= '9')
            tmp += ch;
    }
    return tmp;
}

// ============================================================================
// randomness (SecureRandom.h -- direct OS CSPRNG, see its header)
// ============================================================================

void OTP::entropySleep()
{
    // Optionally sleep for a bit every kFetchedEntropyQuota digits, to give
    // the OS's entropy pool time to refill on constrained hardware. A
    // no-op unless setEntropyGatheringSleepTime() has been called.
    if (m_entropyGatheringSleepTime <= 0)
        return;

    ++m_fetchedEntropyCount;
    if (m_fetchedEntropyCount > kFetchedEntropyQuota) {
        std::this_thread::sleep_for(std::chrono::seconds(m_entropyGatheringSleepTime));
        m_fetchedEntropyCount = 0;
    }
}

char OTP::randDigit()
{
    entropySleep();
    return secure_random::randDigit();
}

std::string OTP::randDigits(int count)
{
    std::string tmp;
    tmp.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        tmp += randDigit();
    return tmp;
}

// ============================================================================
// formatting
// ============================================================================

std::string OTP::codeGroups(const std::string &s, int groupSize, int groupsPerLine, int linesPerPage)
{
    std::string tmp;
    int lineCount = 0;
    int groupCount = 0;

    for (std::size_t x = 0; x < s.length(); x += static_cast<std::size_t>(groupSize)) {
        tmp += s.substr(x, static_cast<std::size_t>(groupSize)) + " ";
        ++groupCount;

        if (groupCount >= groupsPerLine) {
            tmp += "\n";
            groupCount = 0;
            ++lineCount;

            if (lineCount >= linesPerPage) {
                tmp += "\n";
                lineCount = 0;
            }
        }
    }
    return tmp;
}

// ============================================================================
// Morse-cut substitution (off unless setUseMorseShorts(true))
// ============================================================================

namespace {

const std::map<char, char> &morseCutLetter()
{
    static const std::map<char, char> tbl = {
        {'0', 'T'}, {'1', 'A'}, {'2', 'U'}, {'3', 'V'}, {'4', '4'},
        {'5', 'E'}, {'6', '6'}, {'7', 'B'}, {'8', 'D'}, {'9', 'N'},
    };
    return tbl;
}
const std::map<char, char> &morseCutNumber()
{
    static const std::map<char, char> tbl = {
        {'T', '0'}, {'A', '1'}, {'U', '2'}, {'V', '3'}, {'4', '4'},
        {'E', '5'}, {'6', '6'}, {'B', '7'}, {'D', '8'}, {'N', '9'},
    };
    return tbl;
}

} // namespace

std::string OTP::toMorseCut(const std::string &s) const
{
    if (!m_useMorseShorts)
        return s;
    std::string tmp;
    for (char ch : s) {
        if (ch == ' ' || ch == '\n')
            continue;
        const auto it = morseCutLetter().find(ch);
        tmp += (it != morseCutLetter().end()) ? it->second : '?';
    }
    return tmp;
}

std::string OTP::fromMorseCut(const std::string &s) const
{
    if (!m_useMorseShorts)
        return s;
    std::string tmp;
    for (char ch : s) {
        const char up = (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - ('a' - 'A')) : ch;
        if (up == ' ' || up == '\n')
            continue;
        const auto it = morseCutNumber().find(up);
        tmp += (it != morseCutNumber().end()) ? it->second : '?';
    }
    return tmp;
}

// ============================================================================
// reference tables -- for the "-hh" full help text (see OtpCli.cpp's
// printFullHelp()). Mirrors otp.py's/Quacque's appended "REFERENCE TABLES"
// section: the straddling-checkerboard digit-code<->letter tables (Roman and
// Cyrillic) and the Morse cut shorts substitution, for quick lookup while
// working through a message by hand.
// ============================================================================

namespace {

// give the unprintable/whitespace table entries a readable name (a literal
// space or newline is invisible in a table cell otherwise).
std::string displayChar(char32_t ch)
{
    if (ch == U'\n') return "<newline>";
    if (ch == U' ') return "<space>";
    return utf8::encode(std::u32string(1, ch));
}

// Right-justify a (always ASCII, 1-2 digit) checkerboard code to width 2 --
// the only fixed-width column in these tables; the letter/Cyrillic side is
// never padded, so byte-vs-column width for multi-byte UTF-8 never matters.
std::string padCode2(const std::string &code)
{
    return code.size() < 2 ? (" " + code) : code;
}

// "code = letter", sorted by code -- std::map<std::string,...> iterates in
// string-sorted order, which for this table's codes ("0".."6" then
// "70".."99") already matches the intended single-digits-before-double-
// digits, then-numeric order (every double-digit code's first character is
// '7'-'9', which sorts after every single digit code's only character).
std::string formatCodeLetterTable(const std::map<std::string, char32_t> &number2letter, int columns = 5)
{
    std::string out;
    std::vector<std::string> row;
    for (const auto &kv : number2letter) {
        row.push_back(padCode2(kv.first) + " = " + displayChar(kv.second));
        if (static_cast<int>(row.size()) == columns) {
            out += join(row, "  ") + "\n";
            row.clear();
        }
    }
    if (!row.empty())
        out += join(row, "  ") + "\n";
    return out;
}

// "letter = code" -- the inverse of formatCodeLetterTable() above, sorted by
// codepoint (std::map<char32_t,...> iterates in numeric key order).
std::string formatLetterCodeTable(const std::map<std::string, char32_t> &number2letter, int columns = 5)
{
    std::map<char32_t, std::string> letter2number;
    for (const auto &kv : number2letter)
        letter2number[kv.second] = kv.first;

    std::string out;
    std::vector<std::string> row;
    for (const auto &kv : letter2number) {
        row.push_back(displayChar(kv.first) + " = " + padCode2(kv.second));
        if (static_cast<int>(row.size()) == columns) {
            out += join(row, "  ") + "\n";
            row.clear();
        }
    }
    if (!row.empty())
        out += join(row, "  ") + "\n";
    return out;
}

// "key = value" for the Morse cut shorts tables -- works for either
// direction, since morseCutLetter() and morseCutNumber() are both
// std::map<char, char>.
std::string formatMorseTable(const std::map<char, char> &tbl, int columns = 5)
{
    std::string out;
    std::vector<std::string> row;
    for (const auto &kv : tbl) {
        row.push_back(std::string(1, kv.first) + " = " + std::string(1, kv.second));
        if (static_cast<int>(row.size()) == columns) {
            out += join(row, "  ") + "\n";
            row.clear();
        }
    }
    if (!row.empty())
        out += join(row, "  ") + "\n";
    return out;
}

// Standard International Morse Code, reference only -- covers the ten
// digits plus the specific eight letters morseCutLetter() substitutes in
// for a digit ('4' and '6' are left as themselves, so no letter entry is
// needed for those). Not used by toMorseCut()/fromMorseCut() above, which
// only ever substitute one character for another and never touch actual
// dot/dash sequences -- this exists purely so formatMorseToCutTable() below
// can show why the substitution saves time on the wire (every digit's own
// Morse code is a full five symbols; its cut substitute is often shorter).
const std::map<char, std::string> &morseCodeDigits()
{
    static const std::map<char, std::string> tbl = {
        {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
        {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    };
    return tbl;
}
const std::map<char, std::string> &morseCodeLetters()
{
    static const std::map<char, std::string> tbl = {
        {'A', ".-"}, {'B', "-..."}, {'D', "-.."}, {'E', "."},
        {'N', "-."}, {'T', "-"}, {'U', "..-"}, {'V', "...-"},
    };
    return tbl;
}

std::string padRight(const std::string &s, std::size_t width)
{
    return s.size() < width ? s + std::string(width - s.size(), ' ') : s;
}

// render the "digit's own Morse code -> Morse cut shorts letter's (shorter)
// Morse code" table, showing at a glance why each substitution is worth it.
std::string formatMorseToCutTable()
{
    std::string out = padRight("Digit", 5) + " " + padRight("Morse", 7) + " " + padRight("Cut", 4) + " " + "Cut Morse" + "\n"
                     + padRight("-----", 5) + " " + padRight("-----", 7) + " " + padRight("---", 4) + " " + "---------" + "\n";

    for (const auto &kv : morseCutLetter()) {
        const char digit = kv.first;
        const char cutLetter = kv.second;
        const auto letterIt = morseCodeLetters().find(cutLetter);
        const std::string cutMorse = (letterIt != morseCodeLetters().end())
            ? letterIt->second : morseCodeDigits().at(cutLetter);
        out += padRight(std::string(1, digit), 5) + " " + padRight(morseCodeDigits().at(digit), 7) + " "
             + padRight(std::string(1, cutLetter), 4) + " " + cutMorse + "\n";
    }
    return out;
}

} // namespace

std::string OTP::referenceTablesText()
{
    std::string out =
        "\n"
        "*********************************************************\n"
        "* REFERENCE TABLES                                      *\n"
        "*********************************************************\n"
        "\n"
        "These are the straddling-checkerboard digit-code tables used by encode()/decode()\n"
        "(Roman and Cyrillic), plus the Morse cut shorts substitution used when \"-z\" is on.\n"
        "Kept here for quick lookup while working through a message by hand.\n"
        "\n"
        "Roman: digit-code -> letter\n";
    out += formatCodeLetterTable(number2latTable());
    out += "\nRoman: letter -> digit-code\n";
    out += formatLetterCodeTable(number2latTable());
    out += "\nCyrillic: digit-code -> letter\n";
    out += formatCodeLetterTable(number2cyrTable());
    out += "\nCyrillic: letter -> digit-code\n";
    out += formatLetterCodeTable(number2cyrTable());
    out += "\nMorse cut shorts: digit -> letter\n";
    out += formatMorseTable(morseCutLetter());
    out += "\nMorse cut shorts: letter -> digit\n";
    out += formatMorseTable(morseCutNumber());
    out += "\nMorse -> Morse cut shorts (each digit's own Morse code vs. its shorter substitute)\n";
    out += formatMorseToCutTable();
    return out;
}

// ============================================================================
// checkerboard encode/decode -- kept as a near-literal transliteration of
// Quacque's (and otp.py's) index arithmetic (see decode() especially): this
// is exactly the kind of code where a "cleaner" rewrite risks a subtle
// off-by-one, so the same control flow/increments are kept rather than
// restructured.
// ============================================================================

std::u32string OTP::insertAlphabetSwitches(const std::u32string &s) const
{
    std::u32string tmp;
    bool usingRoman = true;

    for (char32_t ch : s) {
        if (ch == U'~') {
            usingRoman = !usingRoman;
            tmp += ch;
            continue;
        }

        const bool inLat = m_lat2number.find(ch) != m_lat2number.end();
        const bool inCyr = m_cyr2number.find(ch) != m_cyr2number.end();

        if (inLat && !inCyr && !usingRoman) {
            tmp += U'~';
            usingRoman = true;
        } else if (inCyr && !inLat && usingRoman) {
            tmp += U'~';
            usingRoman = false;
        }

        tmp += ch;
    }
    return tmp;
}

std::string OTP::encode(const std::u32string &plain, bool *ok)
{
    if (ok) *ok = true;

    bool usingRoman = true;
    bool usingDigits = false;
    std::u32string s = utf8::toUpper(plain);

    if (!stringValid(s)) {
        setError("invalid characters in string: " + utf8::encode(s));
        if (ok) *ok = false;
        return std::string();
    }

    s = insertAlphabetSwitches(s);
    const std::size_t lenS = s.length();
    std::string tmp;

    const std::map<char32_t, std::string> *currentTbl = &m_lat2number;

    std::size_t x = 0;
    while (x < lenS) {
        const char32_t ch = s[x];

        if (ch == U'#') {
            usingDigits = !usingDigits;
            tmp += "94";
        } else if (ch == U'~') {
            tmp += "99";
            usingRoman = !usingRoman;
            currentTbl = usingRoman ? &m_lat2number : &m_cyr2number;
        } else if (ch >= U'0' && ch <= U'9') {
            if (!usingDigits) {
                if (m_autoInsertDigitShiftCode) {
                    usingDigits = true;
                    tmp += "94";
                } else {
                    setError("missing digit code '#' at start of number sequence");
                    if (ok) *ok = false;
                    return std::string();
                }
            }
            const char digitChar = static_cast<char>('0' + (ch - U'0'));
            tmp += digitChar;
            tmp += digitChar;
        } else {
            if (usingDigits) {
                if (m_autoInsertDigitShiftCode) {
                    usingDigits = false;
                    tmp += "94";
                } else {
                    setError("missing '#' code to end number sequence");
                    if (ok) *ok = false;
                    return std::string();
                }
            }

            const auto it = currentTbl->find(ch);
            if (it == currentTbl->end()) {
                const std::string alphabetName = usingRoman ? "Roman" : "Cyrillic";
                setError("letter '" + utf8::encode(std::u32string(1, ch)) + "' not in the "
                          + alphabetName + " alphabet - missing '~'?");
                if (ok) *ok = false;
                return std::string();
            }
            tmp += it->second;
        }
        ++x;
    }

    if (usingDigits) {
        if (m_autoInsertDigitShiftCode) {
            tmp += "94";
        } else {
            setError("missing final '#' code to end number sequence");
            if (ok) *ok = false;
            return std::string();
        }
    }

    if (!usingRoman)
        tmp += "99";

    return tmp;
}

std::u32string OTP::decode(const std::string &digits, bool *ok)
{
    if (ok) *ok = true;

    const std::string s = stringDigits(digits);
    const std::size_t lenS = s.length();
    bool usingRoman = true;
    std::u32string tmp;
    std::string code;
    std::size_t x = 0;

    const std::map<std::string, char32_t> *number2letter = &m_number2lat;

    while (x < lenS) {
        code = s.substr(x, 1);
        if (code > "6") {
            // double-digit code
            ++x;
            if (x < lenS) {
                code += s[x];
            } else {
                setError("string missing a character while decoding: " + digits);
                if (ok) *ok = false;
                return std::u32string();
            }
        }

        if (code == "94") {
            // '#' is a control code, not part of the message -- tracked here
            // but not written to the output, same as '99' below, so decoded
            // text comes back clean without the caller having to strip it.
            ++x;
            code.clear();
            while (code != "94") {
                if (lenS < x + 2) {
                    setError("numbers are short a digit: " + digits);
                    if (ok) *ok = false;
                    return std::u32string();
                }
                code = s.substr(x, 2);

                // all ten "number" codes are a digit doubled ("00".."99");
                // anything else here is either the '94' shift-close or an error.
                if (code.size() == 2 && code[0] == code[1] && code[0] >= '0' && code[0] <= '9') {
                    tmp += static_cast<char32_t>(static_cast<unsigned char>(code[0]));
                } else if (code == "94") {
                    x -= 1;
                } else {
                    setError("error decoding digit run: " + digits);
                    if (ok) *ok = false;
                    return std::u32string();
                }
                x += 2;
            }
        } else if (code == "99") {
            usingRoman = !usingRoman;
            number2letter = usingRoman ? &m_number2lat : &m_number2cyr;
        } else {
            const auto it = number2letter->find(code);
            if (it == number2letter->end()) {
                setError("code '" + code + "' is not assigned to any letter in this alphabet: " + digits);
                if (ok) *ok = false;
                return std::u32string();
            }
            tmp += it->second;
        }

        ++x;
    }

    return tmp;
}

// ============================================================================
// key-file I/O (UTF-8 bytes on disk, same as otp.py/Quacque)
// ============================================================================

std::string OTP::readFile(const std::string &fn, bool *ok)
{
    if (ok) *ok = true;
    std::ifstream f(fn, std::ios::binary);
    if (!f) {
        setError("error reading file: " + fn);
        if (ok) *ok = false;
        return std::string();
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool OTP::writeFile(const std::string &fn, const std::string &s)
{
    if (m_testingMode) {
        setError("TESTING MODE: writeFile() did not write: " + fn);
        return true; // logged via lastError(), not a failure -- matches otp.py's dbg()-and-return
    }

    const std::filesystem::path p(fn);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            setError("error creating directory: " + p.parent_path().string());
            return false;
        }
    }

    std::FILE *f = std::fopen(fn.c_str(), "wb");
    if (!f) {
        setError("error writing file: " + fn);
        return false;
    }
    std::fwrite(s.data(), 1, s.size(), f);
    std::fflush(f);
    forceFlushToDisk(f);
    std::fclose(f);
    return true;
}

std::string OTP::loadKeyPad(const std::string &fn, bool *ok)
{
    const std::string raw = readFile(fn, ok);
    if (ok && !*ok)
        return std::string();
    return stringDigits(raw);
}

std::string OTP::loadKeys(const std::vector<std::string> &keyFiles, bool *ok)
{
    if (ok) *ok = true;
    std::string key;
    for (const std::string &kfn : keyFiles) {
        bool readOk = true;
        key += readFile(kfn, &readOk);
        if (!readOk) {
            if (ok) *ok = false;
            return std::string();
        }
    }
    return stringDigits(key);
}

bool OTP::wipeFile(const std::string &fn)
{
    if (m_testingMode) {
        setError("TESTING MODE: wipeFile() did not execute: " + fn);
        return true;
    }

    std::error_code ec;
    const std::uintmax_t fs = std::filesystem::file_size(fn, ec);
    if (ec) {
        setError("error opening file to wipe: " + fn);
        return false;
    }

    std::FILE *f = std::fopen(fn.c_str(), "r+b");
    if (!f) {
        setError("error opening file to wipe: " + fn);
        return false;
    }

    // all ones, all zeroes, a spread of hex patterns, then zeroes again --
    // same pattern list as otp.py's/Quacque's wipeFile().
    static const unsigned char patterns[] = {
        0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
    };

    std::vector<unsigned char> buf(static_cast<std::size_t>(fs));
    for (int r = 0; r < m_wipeRoundCount; ++r) {
        for (unsigned char byteVal : patterns) {
            std::fill(buf.begin(), buf.end(), byteVal);
            std::fseek(f, 0, SEEK_SET);
            std::fwrite(buf.data(), 1, buf.size(), f);
            std::fflush(f);
            forceFlushToDisk(f);
        }
    }
    std::fclose(f);

    std::filesystem::remove(fn, ec);
    if (ec) {
        setError("error removing wiped file: " + fn);
        return false;
    }
    return true;
}

bool OTP::wipeKeys(const std::vector<std::string> &keyFiles)
{
    if (m_keepKeyFilesAfterUse) {
        setError("warning: key files kept after use (keepKeyFilesAfterUse is set)");
        return true;
    }

    // Unlike otp.py (which die()s the whole process on the first wipe
    // failure, potentially leaving later key files un-wiped), keep going
    // through every file and report the last failure -- more of the spent
    // keys end up actually wiped this way, which matters more than which
    // exact error message survives.
    bool allOk = true;
    for (const std::string &kfn : keyFiles) {
        if (!wipeFile(kfn))
            allOk = false;
    }
    return allOk;
}

// ============================================================================
// key generation
// ============================================================================

bool OTP::keygen(const std::string &prefix, bool zeroKeys, int pagesPerPad, bool trigon)
{
    clearError();
    if (prefix.empty()) {
        setError("no key prefix given");
        return false;
    }

    // "-kt": CIA TRIGON-size sheets (8 groups x 40 lines, 1600 digits/page)
    // instead of the default (shorter) sheet size.
    const int groupsPerLine = trigon ? kTrigonGroupsPerLine : kGroupsPerLine;
    const int linesPerPage  = trigon ? kTrigonLinesPerPage  : kLinesPerPage;
    const int sheetSize     = trigon ? kTrigonSheetSize     : kSheetSize;

    for (int x = 1; x <= pagesPerPad; ++x) {
        const std::string fn = prefix + "-" + zeroPad(x, 3) + ".otk";

        std::string tmp;
        if (zeroKeys) {
            tmp = std::string(static_cast<std::size_t>(sheetSize), '0');
        } else {
            // Folds (1 + m_randomDuplicates) fresh random draws together via
            // stringAddDigits() rather than taking just one -- defaults to
            // one draw unless the caller (via "-q") asked for extra
            // whitening rounds.
            tmp = std::string(static_cast<std::size_t>(sheetSize), '0');
            bool ok = true;
            for (int i = 0; i < 1 + m_randomDuplicates; ++i) {
                tmp = stringAddDigits(tmp, randDigits(sheetSize), &ok);
                if (!ok)
                    return false;
            }
        }

        if (!writeFile(fn, codeGroups(tmp, kDigitsPerGroup, groupsPerLine, linesPerPage)))
            return false;
    }
    return true;
}

// ============================================================================
// encipher / decipher
// ============================================================================

std::optional<std::u32string> OTP::encipher(const std::u32string &plainText, const std::vector<std::string> &keyFiles)
{
    clearError();

    bool ok = true;
    const std::string key = loadKeys(keyFiles, &ok);
    if (!ok)
        return std::nullopt;

    const std::u32string inputTxt = utf8::toUpper(plainText);
    const std::string encodedTxt = encode(inputTxt, &ok);
    if (!ok)
        return std::nullopt;

    if (encodedTxt.length() > key.length()) {
        setError("not enough key material: message needs " + std::to_string(encodedTxt.length())
                  + " digits, key has " + std::to_string(key.length()));
        return std::nullopt;
    }

    // Deliberately encodedTxt - key (not key - encodedTxt): this is what
    // lets a message prefixed with "AAAAA" (-> "00000") double as an
    // easy-to-eyeball confirmation of which keypad was used.
    const std::string cipherTxt = stringSubtractDigits(encodedTxt, key, &ok);
    if (!ok)
        return std::nullopt;

    const std::u32string result = utf8::decode(codeGroups(toMorseCut(cipherTxt)));

    // Crypto succeeded -- wipe the spent keys, but a wipe failure is a
    // secondary (if security-relevant) concern, not a reason to withhold the
    // already-correct ciphertext. See lastError()'s doc comment.
    std::string cryptoError;
    if (!wipeKeys(keyFiles))
        cryptoError = m_lastError;

    if (!cryptoError.empty())
        setError(cryptoError);
    else
        clearError();

    return result;
}

std::optional<std::u32string> OTP::decipher(const std::u32string &cipherText, const std::vector<std::string> &keyFiles)
{
    clearError();

    bool ok = true;
    const std::string key = loadKeys(keyFiles, &ok);
    if (!ok)
        return std::nullopt;

    std::string inputTxt = fromMorseCut(utf8::encode(cipherText));
    inputTxt = stringDigits(inputTxt);

    if (inputTxt.length() > key.length()) {
        setError("not enough key material: ciphertext needs " + std::to_string(inputTxt.length())
                  + " digits, key has " + std::to_string(key.length()));
        return std::nullopt;
    }

    const std::string clearTxt = stringAddDigits(inputTxt, key, &ok);
    if (!ok)
        return std::nullopt;

    const std::u32string decodedTxt = decode(clearTxt, &ok);
    if (!ok)
        return std::nullopt;

    std::string cryptoError;
    if (!wipeKeys(keyFiles))
        cryptoError = m_lastError;

    if (!cryptoError.empty())
        setError(cryptoError);
    else
        clearError();

    return decodedTxt;
}

// ============================================================================
// key join / unjoin
// ============================================================================

const std::vector<std::string> &OTP::combo5x10()
{
    // All 5-of-10 combinations of digits 0-9, in lexicographic order --
    // "01234", "01235", ..., "56789" -- 252 entries. Generated once here
    // rather than transcribed as a 252-entry literal table -- this
    // generates the identical order because it's the same lexicographic
    // walk Python's itertools.combinations() takes.
    static const std::vector<std::string> table = [] {
        std::vector<std::string> t;
        t.reserve(252);
        for (int a = 0; a <= 5; ++a)
        for (int b = a + 1; b <= 6; ++b)
        for (int c = b + 1; c <= 7; ++c)
        for (int d = c + 1; d <= 8; ++d)
        for (int e = d + 1; e <= 9; ++e) {
            std::string combo;
            combo += static_cast<char>('0' + a);
            combo += static_cast<char>('0' + b);
            combo += static_cast<char>('0' + c);
            combo += static_cast<char>('0' + d);
            combo += static_cast<char>('0' + e);
            t.push_back(combo);
        }
        return t;
    }();
    return table;
}

std::vector<std::string> OTP::combinateExpandedKeys(const std::string &keyInput, bool *ok)
{
    if (ok) *ok = true;

    std::string rows[10];
    for (int d = 0; d < 10; ++d)
        rows[d] = keyInput.substr(static_cast<std::size_t>(d) * 25, 25);

    std::vector<std::string> retVal;
    retVal.reserve(252);

    for (const std::string &combo : combo5x10()) {
        std::string s(25, '0');
        bool subOk = true;
        for (char digitCh : combo) {
            s = stringSubtractDigits(s, rows[digitCh - '0'], &subOk);
            if (!subOk) {
                if (ok) *ok = false;
                return {};
            }
        }

        bool addOk = true;
        std::string checkSum = s.substr(0, 5);
        checkSum = stringAddDigits(checkSum, s.substr(5, 5), &addOk);
        checkSum = stringAddDigits(checkSum, s.substr(10, 5), &addOk);
        checkSum = stringAddDigits(checkSum, s.substr(15, 5), &addOk);
        checkSum = stringAddDigits(checkSum, s.substr(20, 5), &addOk);
        if (!addOk) {
            if (ok) *ok = false;
            return {};
        }

        retVal.push_back(s + checkSum);
    }
    return retVal;
}

// joinKeys() and unjoinKeys() share everything up through recovering the
// 504-row, checksum-sorted, halved key table -- identical in both
// directions. Only what happens with the two halves afterward differs: join
// masks them with fresh randomness and emits both; unjoin recovers that same
// randomness from an already-received combinedKeyFile.

bool OTP::joinKeys(const std::string &fileI, const std::string &fileJ,
                    const std::string &combinedKeyFile, const std::string &prefix)
{
    clearError();
    if (prefix.empty()) {
        setError("no key prefix given");
        return false;
    }

    bool ok = true;
    const std::string ki = loadKeyPad(fileI, &ok);
    if (!ok || static_cast<int>(ki.length()) != kSheetSize) {
        setError("bad first key (expected " + std::to_string(kSheetSize) + " digits, got " + std::to_string(ki.length()) + ")");
        return false;
    }
    const std::string kj = loadKeyPad(fileJ, &ok);
    if (!ok || static_cast<int>(kj.length()) != kSheetSize) {
        setError("bad second key (expected " + std::to_string(kSheetSize) + " digits, got " + std::to_string(kj.length()) + ")");
        return false;
    }

    const std::vector<std::string> iTmp = combinateExpandedKeys(ki, &ok);
    if (!ok) return false;
    const std::vector<std::string> jTmp = combinateExpandedKeys(kj, &ok);
    if (!ok) return false;

    std::vector<std::string> tmp = iTmp;
    tmp.insert(tmp.end(), jTmp.begin(), jTmp.end()); // 504 rows of 30 digits (25 + 5 checksum)

    // index by the trailing 5-digit checksum, resolving collisions by
    // walking forward mod 100000 -- must match unjoinKeys()'s walk exactly.
    std::map<std::string, std::string> keys;
    for (const std::string &row : tmp) {
        std::string checkSum = row.substr(row.length() - 5);
        const std::string value = row.substr(0, row.length() - 5);

        while (keys.find(checkSum) != keys.end()) {
            const int csNum = (std::stoi(checkSum) + 1) % 100000;
            checkSum = zeroPad(csNum, 5);
        }
        keys[checkSum] = value;
    }

    // std::map keeps keys sorted already, so walking it in order is exactly
    // Python's explicit "for k in sorted(keys.keys())" walk.
    std::vector<std::string> sortedVals;
    sortedVals.reserve(keys.size());
    for (const auto &kv : keys)
        sortedVals.push_back(kv.second);

    const std::size_t half = sortedVals.size() / 2;
    const std::vector<std::string> keysA(sortedVals.begin(), sortedVals.begin() + static_cast<long>(half));
    const std::vector<std::string> keysB(sortedVals.begin() + static_cast<long>(half), sortedVals.end());

    std::string newOtpTbl;
    std::string newRandomKeyStr;
    newOtpTbl.reserve(half * 25);
    newRandomKeyStr.reserve(half * 25);

    for (std::size_t x = 0; x < half; ++x) {
        std::string tmp_s = stringAddDigits(keysA[x], keysB[x], &ok);
        if (!ok) return false;

        // freshly generated random pad material -- this, not the ciphertext
        // below, is the new keypad delivered locally in the clear further
        // down.
        const std::string rd = randDigits(25);
        newRandomKeyStr += rd;

        tmp_s = stringSubtractDigits(tmp_s, rd, &ok);
        if (!ok) return false;
        newOtpTbl += tmp_s;
    }

    // combinedKeyFile: sent to the distant recipient, who reconstructs
    // newRandomKeyStr from it via unjoinKeys().
    if (!writeFile(combinedKeyFile, codeGroups(newOtpTbl)))
        return false;

    // NOTE (carried over from otp.py/Quacque, not fixed here): newRandomKeyStr
    // is half*25 digits (6300 for the default 252-row half), but only the
    // first kPadSize (6250) digits get written out below as the 25 new
    // sheets -- the trailing ~50 digits of freshly generated randomness are
    // computed, folded into newOtpTbl above, and then simply never saved by
    // either side. Both joinKeys() and unjoinKeys() apply the same
    // truncation, so the two sides stay in sync; it just means a little of
    // the generated entropy goes unused rather than becoming key material.
    // See kwak_design.md.
    for (int i = 0; i < kPagesPerPad; ++i) {
        const std::string tmpStr = newRandomKeyStr.substr(static_cast<std::size_t>(i) * kSheetSize, kSheetSize);
        const std::string filename = prefix + "-" + zeroPad(i + 1, 3) + ".otk";
        if (!writeFile(filename, codeGroups(tmpStr)))
            return false;
    }

    std::string wipeError;
    if (!wipeKeys({fileI, fileJ}))
        wipeError = m_lastError;
    if (!wipeError.empty())
        setError(wipeError);
    else
        clearError();

    return true;
}

bool OTP::unjoinKeys(const std::string &fileI, const std::string &fileJ,
                      const std::string &combinedKeyFile, const std::string &prefix)
{
    clearError();
    if (prefix.empty()) {
        setError("no key prefix given");
        return false;
    }

    bool ok = true;
    const std::string ki = loadKeyPad(fileI, &ok);
    if (!ok || static_cast<int>(ki.length()) != kSheetSize) {
        setError("bad first key (expected " + std::to_string(kSheetSize) + " digits, got " + std::to_string(ki.length()) + ")");
        return false;
    }
    const std::string kj = loadKeyPad(fileJ, &ok);
    if (!ok || static_cast<int>(kj.length()) != kSheetSize) {
        setError("bad second key (expected " + std::to_string(kSheetSize) + " digits, got " + std::to_string(kj.length()) + ")");
        return false;
    }

    const std::vector<std::string> iTmp = combinateExpandedKeys(ki, &ok);
    if (!ok) return false;
    const std::vector<std::string> jTmp = combinateExpandedKeys(kj, &ok);
    if (!ok) return false;

    std::vector<std::string> tmp = iTmp;
    tmp.insert(tmp.end(), jTmp.begin(), jTmp.end());

    std::map<std::string, std::string> keys;
    for (const std::string &row : tmp) {
        std::string checkSum = row.substr(row.length() - 5);
        const std::string value = row.substr(0, row.length() - 5);

        while (keys.find(checkSum) != keys.end()) {
            const int csNum = (std::stoi(checkSum) + 1) % 100000;
            checkSum = zeroPad(csNum, 5);
        }
        keys[checkSum] = value;
    }

    std::vector<std::string> sortedVals;
    sortedVals.reserve(keys.size());
    for (const auto &kv : keys)
        sortedVals.push_back(kv.second);

    const std::size_t half = sortedVals.size() / 2;
    const std::vector<std::string> keysA(sortedVals.begin(), sortedVals.begin() + static_cast<long>(half));
    const std::vector<std::string> keysB(sortedVals.begin() + static_cast<long>(half), sortedVals.end());

    std::string combinedKeys;
    combinedKeys.reserve(half * 25);
    for (std::size_t x = 0; x < half; ++x) {
        combinedKeys += stringAddDigits(keysA[x], keysB[x], &ok);
        if (!ok) return false;
    }

    const std::string keyInput = loadKeyPad(combinedKeyFile, &ok);
    if (!ok) return false;
    if (keyInput.length() != combinedKeys.length()) {
        setError("combined key file length does not match the expected keypad length");
        return false;
    }

    // invert joinKeys()'s masking step: ct = K - rd, so rd = K - ct
    const std::string newRandomKeyStr = stringSubtractDigits(combinedKeys, keyInput, &ok);
    if (!ok) return false;

    for (int i = 0; i < kPagesPerPad; ++i) {
        const std::string tmpStr = newRandomKeyStr.substr(static_cast<std::size_t>(i) * kSheetSize, kSheetSize);
        const std::string filename = prefix + "-" + zeroPad(i + 1, 3) + ".otk";
        if (!writeFile(filename, codeGroups(tmpStr)))
            return false;
    }

    std::string wipeError;
    if (!wipeKeys({fileI, fileJ}))
        wipeError = m_lastError;
    if (!wipeError.empty())
        setError(wipeError);
    else
        clearError();

    return true;
}

// ============================================================================
// known-plaintext key recovery ("-f")
// ============================================================================

bool OTP::generateKeyForKnownPlaintext(const std::u32string &plainText, const std::u32string &cipherText,
                                        const std::string &keyOutputFile)
{
    clearError();
    bool ok = true;

    const std::string msgP = encode(plainText, &ok);
    if (!ok)
        return false;

    const std::string msgC = stringDigits(fromMorseCut(utf8::encode(cipherText)));

    if (msgC.length() != msgP.length()) {
        setError("ciphertext and encoded plaintext must be the same length "
                  "(ciphertext: " + std::to_string(msgC.length()) + " digits, plaintext: "
                  + std::to_string(msgP.length()) + " digits)");
        return false;
    }

    const std::string kStr = stringSubtractDigits(msgP, msgC, &ok);
    if (!ok)
        return false;

    return writeFile(keyOutputFile, codeGroups(kStr));
}

// ============================================================================
// general m-of-n combinations, message split / merge
// ============================================================================

std::vector<std::string> OTP::combo(int m, int n)
{
    // Standard lexicographic "next combination" walk over {1..n} choose m --
    // produces the same order as Python's itertools.combinations(range(1,
    // n+1), m).
    std::vector<std::string> result;
    if (m <= 0 || m > n)
        return result;

    std::vector<int> idx(static_cast<std::size_t>(m));
    for (int i = 0; i < m; ++i)
        idx[static_cast<std::size_t>(i)] = i; // 0-based; courier number is idx[i] + 1

    while (true) {
        std::string s;
        for (int i = 0; i < m; ++i)
            s += std::to_string(idx[static_cast<std::size_t>(i)] + 1);
        result.push_back(s);

        int i = m - 1;
        while (i >= 0 && idx[static_cast<std::size_t>(i)] == n - m + i)
            --i;
        if (i < 0)
            break; // that was the last combination
        ++idx[static_cast<std::size_t>(i)];
        for (int j = i + 1; j < m; ++j)
            idx[static_cast<std::size_t>(j)] = idx[static_cast<std::size_t>(j - 1)] + 1;
    }
    return result;
}

std::vector<std::string> OTP::splitIntoShares(const std::string &msgDigits, int shareCount)
{
    // shareCount-1 fresh random strings, plus the message minus their sum --
    // so all shareCount shares are needed to recover msgDigits (any
    // shareCount-1 of them reveal nothing about it).
    std::vector<std::string> tbl;
    tbl.reserve(static_cast<std::size_t>(shareCount));

    std::string tmp = msgDigits;
    for (int i = 0; i < shareCount - 1; ++i) {
        const std::string r = randDigits(static_cast<int>(msgDigits.length()));
        tbl.push_back(r);
        bool ok = true;
        tmp = stringSubtractDigits(tmp, r, &ok);
        // r and tmp are always the same length here (both msgDigits.length()),
        // so stringSubtractDigits() can't actually fail on the length check.
    }
    tbl.push_back(tmp);
    return tbl;
}

bool OTP::splitMessage(const std::u32string &plainText, int minParts, int maxParts, const std::string &filePrefix)
{
    clearError();

    if (minParts < 1 || maxParts < minParts) {
        setError("minParts/maxParts must satisfy 1 <= minParts <= maxParts");
        return false;
    }
    if (maxParts > 9) {
        // Each courier group is named by indexing into the combination's
        // concatenated-digit-string ID by character position -- that only
        // holds while every courier number is a single digit. Refuse it
        // outright past that rather than reproduce the silent misbehavior
        // otp.py/Quacque flag for this case. See kwak_design.md.
        setError("maxParts > 9 is not supported (courier numbering becomes ambiguous past single digits)");
        return false;
    }
    if (plainText.empty()) {
        setError("message is empty, nothing to split");
        return false;
    }

    bool ok = true;
    const std::string encoded = encode(plainText, &ok);
    if (!ok)
        return false;
    const std::string clearTextDigits = stringDigits(encoded);

    for (const std::string &messageGroup : combo(minParts, maxParts)) {
        const std::vector<std::string> shares = splitIntoShares(clearTextDigits, minParts);

        for (std::size_t i = 0; i < messageGroup.length(); ++i) {
            const std::string fileName = filePrefix + messageGroup[i] + "-" + messageGroup + ".otp";
            const std::string fileData = codeGroups(shares[i]) + "\n";
            if (!writeFile(fileName, fileData))
                return false;
        }
    }

    clearError();
    return true;
}

std::optional<std::u32string> OTP::mergeMessage(const std::vector<std::string> &segmentFiles)
{
    clearError();

    if (segmentFiles.empty()) {
        setError("no segment files given");
        return std::nullopt;
    }

    bool ok = true;
    const std::string firstText = readFile(segmentFiles.front(), &ok);
    if (!ok)
        return std::nullopt;

    const std::size_t fileLen = stringDigits(firstText).length();
    if (fileLen < 1) {
        setError("segment file " + segmentFiles.front() + " contains no text");
        return std::nullopt;
    }

    // matches otp.py's/Quacque's behavior exactly, including reading
    // segmentFiles.front() again inside this loop -- redundant, but this is
    // a faithful port, not a rewrite.
    std::string clearTextDigits(fileLen, '0');
    for (const std::string &filename : segmentFiles) {
        const std::string raw = readFile(filename, &ok);
        if (!ok)
            return std::nullopt;
        const std::string tmpText = stringDigits(raw);

        if (tmpText.length() != clearTextDigits.length()) {
            setError("message length of file " + filename + " does not match");
            return std::nullopt;
        }
        clearTextDigits = stringAddDigits(clearTextDigits, tmpText, &ok);
        if (!ok)
            return std::nullopt;
    }

    const std::u32string plainText = decode(clearTextDigits, &ok);
    if (!ok)
        return std::nullopt;

    std::string wipeError;
    if (!wipeKeys(segmentFiles))
        wipeError = m_lastError;
    if (!wipeError.empty())
        setError(wipeError);
    else
        clearError();

    return plainText;
}

// ============================================================================
// stream combine ("-b")
// ============================================================================

bool OTP::combineStreams(const std::string &inputFile1, const std::string &inputFile2,
                          const std::string &combinedFile, const std::string &keyPrefix)
{
    clearError();
    bool ok = true;

    const std::string tmp1 = stringDigits(readFile(inputFile1, &ok));
    if (!ok)
        return false;
    const std::string tmp2 = stringDigits(readFile(inputFile2, &ok));
    if (!ok)
        return false;

    if (tmp1.length() != tmp2.length()) {
        setError("code stream digit counts are different");
        return false;
    }

    const std::string tmp3 = stringAddDigits(tmp1, tmp2, &ok);
    if (!ok)
        return false;

    if (!combinedFile.empty()) {
        if (!writeFile(combinedFile, codeGroups(tmp3)))
            return false;
    }

    if (!keyPrefix.empty()) {
        // 0-based page numbering here, unlike keygen()'s 1-based -- matches
        // otp.py's/Quacque's do_combineStreams() exactly.
        const int digitsNeeded = static_cast<int>(tmp3.length());
        const int pageCount = digitsNeeded / kSheetSize;
        for (int x = 0; x < pageCount; ++x) {
            const std::string fn = keyPrefix + "-" + zeroPad(x, 2) + ".otk";
            const std::string s = tmp3.substr(static_cast<std::size_t>(x) * kSheetSize, kSheetSize);
            if (!writeFile(fn, codeGroups(s)))
                return false;
        }
    }

    std::string wipeError;
    if (!wipeKeys({inputFile1, inputFile2}))
        wipeError = m_lastError;
    if (!wipeError.empty())
        setError(wipeError);
    else
        clearError();

    return true;
}

// ============================================================================
// wipe ("-w")
// ============================================================================

bool OTP::wipeFiles(const std::vector<std::string> &files)
{
    clearError();
    bool allOk = true;
    for (const std::string &fn : files) {
        if (!wipeFile(fn))
            allOk = false;
    }
    if (!allOk && m_lastError.empty())
        setError("one or more files failed to wipe");
    return allOk;
}
