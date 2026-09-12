// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#pragma once

// OTP -- core SilentDuck cipher engine. Ported from Quacque's OTP.h/OTP.cpp
// (the Qt/C++ port of otp.py), with every Qt type replaced by a standard
// C++17 equivalent -- see ../kwak_design.md for the full otp.py -> Quacque ->
// Kwak lineage and the Qt-type -> std-type translation table this follows.
//
// Full functional parity with otp.py's (and Quacque's) cipher/key
// operations: key generation ("-g"/"-gz"/"-kt"), encipher/decipher
// ("-e"/"-d"), key-join/unjoin ("-j"/"-u"), known-plaintext key recovery
// ("-f"), message split/merge ("-s"/"-m"), stream-combine ("-b"), and wipe
// ("-w"). CLI argument parsing and help text are NOT here -- see
// OtpCli.h/.cpp, the console-facing layer built on this same public API.
//
// Message text is passed in/out as std::u32string (one char32_t per Unicode
// codepoint) rather than file paths or QString -- see Utf8.h for why
// char32_t specifically. Key material stays file-based (.otk files) on
// purpose -- keys are bulky, persistent, and meant to live on removable
// media, handed off by courier rather than transmitted.
//
// Every failure path returns false (or std::nullopt for an
// std::optional<std::u32string>-returning method) and sets lastError() for
// the caller to show -- never a hard exit, matching Quacque's departure from
// otp.py's die()/sys.exit() here.
//
// NOTE: the straddling checkerboard's Cyrillic half is built from char32_t
// numeric literals (U'А' etc.), never embedded literal Cyrillic bytes --
// same reasoning as Quacque's OTP.cpp: numeric escapes can't silently
// corrupt if some future edit/diff/clone mangles the source file's encoding.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

class OTP
{
public:
    OTP();

    // ---- sheet / pad sizing -- mirrors otp.py's/Quacque's module-level constants ----
    static constexpr int kDigitsPerGroup = 5;
    static constexpr int kGroupsPerLine  = 5;
    static constexpr int kLinesPerPage   = 10;
    static constexpr int kSheetSize      = kDigitsPerGroup * kGroupsPerLine * kLinesPerPage; // 250
    static constexpr int kPagesPerPad    = 25;
    static constexpr int kPadSize        = kSheetSize * kPagesPerPad; // 6250

    // Historical CIA TRIGON sheet size ("-kt"): 8 groups of 5 digits per
    // line, 40 lines per page -- 1600 digits/page instead of the shorter
    // default above. Only affects keygen(); kPagesPerPad (25) is unchanged.
    static constexpr int kTrigonGroupsPerLine = 8;
    static constexpr int kTrigonLinesPerPage  = 40;
    static constexpr int kTrigonSheetSize     = kDigitsPerGroup * kTrigonGroupsPerLine * kTrigonLinesPerPage; // 1600

    // ---- configuration (mirrors otp.py's/Quacque's settings) ----

    void setKeepKeyFilesAfterUse(bool keep) { m_keepKeyFilesAfterUse = keep; }
    bool keepKeyFilesAfterUse() const { return m_keepKeyFilesAfterUse; }

    // Number of overwrite passes wipeFile() performs before deleting a key
    // file. Defaults to 7; each pass is forced to physical storage via a
    // real OS-level fsync/FlushFileBuffers, not just a libc-level flush --
    // see forceFlushToDisk() in OTP.cpp.
    void setWipeRoundCount(int rounds) { m_wipeRoundCount = rounds; }
    int wipeRoundCount() const { return m_wipeRoundCount; }

    // When false (default/strict), encode() requires an explicit '#'
    // digit-shift code around runs of digits and fails if one is missing.
    // Set true to have encode() insert the missing '#' automatically.
    void setAutoInsertDigitShiftCode(bool autoInsert) { m_autoInsertDigitShiftCode = autoInsert; }
    bool autoInsertDigitShiftCode() const { return m_autoInsertDigitShiftCode; }

    // "-z": substitute Morse-cut letters for digits on the wire. Off by default.
    void setUseMorseShorts(bool use) { m_useMorseShorts = use; }
    bool useMorseShorts() const { return m_useMorseShorts; }

    // "-t": dry-run mode. writeFile()/wipeFile() log what they would have
    // done and return success without touching the filesystem.
    void setTestingMode(bool testing) { m_testingMode = testing; }
    bool testingMode() const { return m_testingMode; }

    // "-q": how many extra rounds of fresh random material get folded into
    // a freshly generated key sheet during keygen() (0 default = one round).
    void setRandomDuplicates(int n) { m_randomDuplicates = n; }
    int randomDuplicates() const { return m_randomDuplicates; }

    // "-n": sleep this many seconds after every 25 fetched random digits,
    // giving the OS's entropy pool time to refill on constrained hardware.
    // 0 (default) disables it. Blocks the calling thread.
    void setEntropyGatheringSleepTime(int seconds) { m_entropyGatheringSleepTime = seconds; }
    int entropyGatheringSleepTime() const { return m_entropyGatheringSleepTime; }

    // True if ch belongs to the straddling checkerboard's alphabet: Roman
    // A-Z, the Cyrillic letters the checkerboard actually uses (see
    // buildTables()), digits 0-9, and the punctuation/control chars the
    // checkerboard maps (? : @ / ~ # . , space, newline). Case-insensitive.
    // Meant for gating keystrokes in an interactive text-entry UI
    // (TerminalEditor) before they ever reach encode().
    static bool isAllowedInputChar(char32_t ch);

    // The straddling checkerboard's full alphabet, in a fixed display order
    // (Roman letters, punctuation/control chars, digits, then Cyrillic) --
    // the same set isAllowedInputChar() tests membership against. Meant for
    // pasting into TerminalEditor (its F5) when the user can't recall how to
    // type one of its more exotic characters.
    static std::u32string allowedInputChars();

    // Formatted reference tables for the straddling checkerboard (digit-code
    // <-> letter, Roman and Cyrillic) and the Morse cut shorts substitution
    // -- printed at the end of "-hh"'s full help text (see OtpCli.cpp's
    // printFullHelp()). UTF-8 encoded. Self-contained, callable without an
    // OTP instance.
    static std::string referenceTablesText();

    // Message from the most recent call, UTF-8 encoded. Cleared at the start
    // of every call below. NOTE: a call can succeed and still leave a
    // message here -- e.g. the crypto succeeded but wiping a spent key file
    // afterward failed. Always check this, not just after a failure.
    std::string lastError() const { return m_lastError; }

    // ---- key generation ("-g" / "-gz" / "-kt") ----
    // Writes pagesPerPad .otk files named "<prefix>-NNN.otk" (NNN
    // 001..pagesPerPad), each one sheet (kSheetSize digits) of key material.
    // zeroKeys=true generates an all-zero pad -- testing only, NEVER for a
    // real message. trigon=true ("-kt") writes kTrigonSheetSize-digit sheets
    // instead, matching the historical CIA TRIGON page size.
    bool keygen(const std::string &prefix, bool zeroKeys = false, int pagesPerPad = kPagesPerPad, bool trigon = false);

    // ---- encipher / decipher ("-e" / "-d") ----
    // keyFiles is one or more .otk paths, concatenated in order. Spent key
    // files are wiped (unless keepKeyFilesAfterUse()) after a successful
    // call. Returns std::nullopt on failure -- check lastError().
    std::optional<std::u32string> encipher(const std::u32string &plainText, const std::vector<std::string> &keyFiles);
    std::optional<std::u32string> decipher(const std::u32string &cipherText, const std::vector<std::string> &keyFiles);

    // ---- key join / unjoin ("-j" / "-u") ----
    // Combine two kSheetSize-digit key sheets (fileI, fileJ) into a fresh
    // 25-sheet keypad. combinedKeyFile is the in-the-clear table exchanged
    // between the join side and the unjoin side so both derive the same new
    // keypad without ever putting the new keypad itself on the wire.
    // fileI/fileJ are wiped after use (unless keepKeyFilesAfterUse()). Both
    // sides write the 25 new sheets as "<prefix>-NNN.otk".
    bool joinKeys(const std::string &fileI, const std::string &fileJ,
                  const std::string &combinedKeyFile, const std::string &prefix);
    bool unjoinKeys(const std::string &fileI, const std::string &fileJ,
                     const std::string &combinedKeyFile, const std::string &prefix);

    // ---- known-plaintext key recovery ("-f") ----
    // Given a plaintext message and the ciphertext it's known to correspond
    // to, computes the key that would produce that exact pairing and writes
    // it to keyOutputFile. Deliberate feature, not a bug: repudiation -- any
    // OTP ciphertext can be "decrypted" to any plaintext of the same length,
    // given a manufactured key to match.
    bool generateKeyForKnownPlaintext(const std::u32string &plainText, const std::u32string &cipherText,
                                       const std::string &keyOutputFile);

    // ---- message split ("-s") ----
    // Splits plainText into maxParts numbered shares such that any minParts
    // of them reconstruct the message -- a simple additive/subtractive
    // scheme, not OTP-keyed (no key file involved). Writes
    // "<filePrefix><courier>-<group>.otp" for every courier in every
    // minParts-of-maxParts combination group.
    bool splitMessage(const std::u32string &plainText, int minParts, int maxParts,
                       const std::string &filePrefix);

    // ---- message merge ("-m") ----
    // Inverse of splitMessage(): reconstructs the plaintext from at least
    // minParts of the share files written above (order doesn't matter, but
    // they must all belong to the same courier group). Segment files are
    // wiped after use (unless keepKeyFilesAfterUse()). Returns std::nullopt
    // on failure.
    std::optional<std::u32string> mergeMessage(const std::vector<std::string> &segmentFiles);

    // ---- stream combine ("-b") ----
    // Adds two equal-length digit streams together (digit-wise, mod 10, no
    // carry). combinedFile and/or keyPrefix may be empty to skip that
    // output; when keyPrefix is given, the combined stream is also sliced
    // into kSheetSize-digit sheets named "<keyPrefix>-NN.otk". inputFile1/
    // inputFile2 are wiped after use (unless keepKeyFilesAfterUse()).
    bool combineStreams(const std::string &inputFile1, const std::string &inputFile2,
                         const std::string &combinedFile, const std::string &keyPrefix);

    // ---- wipe ("-w") ----
    // Securely wipes an arbitrary list of files, same overwrite-then-delete
    // logic as the internal key-wipe path. Always runs -- not gated by
    // keepKeyFilesAfterUse(), since this is an explicit user request.
    bool wipeFiles(const std::vector<std::string> &files);

private:
    // ---- config state ----
    bool m_keepKeyFilesAfterUse = false;
    int  m_wipeRoundCount = 7;
    bool m_autoInsertDigitShiftCode = true;
    bool m_useMorseShorts = false;
    bool m_testingMode = false;
    int  m_randomDuplicates = 0;
    int  m_entropyGatheringSleepTime = 0;
    int  m_fetchedEntropyCount = 0; // digits fetched since the last entropySleep()
    static constexpr int kFetchedEntropyQuota = 25;
    mutable std::string m_lastError;

    void clearError() { m_lastError.clear(); }
    void setError(const std::string &msg) { m_lastError = msg; }

    // ---- straddling-checkerboard tables (built once, in the constructor) ----
    std::map<std::string, char32_t> m_number2lat;
    std::map<char32_t, std::string> m_lat2number;
    std::map<std::string, char32_t> m_number2cyr;
    std::map<char32_t, std::string> m_cyr2number;
    void buildTables();

    // ---- low-level digit-string arithmetic (add/subtract per-digit, mod 10, no carry) ----
    std::string stringAddDigits(const std::string &a, const std::string &b, bool *ok);
    std::string stringSubtractDigits(const std::string &a, const std::string &b, bool *ok);
    static std::string stringDigits(const std::string &s);
    bool stringValid(const std::u32string &s) const;

    // ---- randomness (SecureRandom.h -- direct OS CSPRNG, see its header) ----
    char randDigit();
    std::string randDigits(int count);
    void entropySleep();

    // ---- checkerboard encode/decode ----
    std::u32string insertAlphabetSwitches(const std::u32string &s) const;
    std::string encode(const std::u32string &plain, bool *ok);
    std::u32string decode(const std::string &digits, bool *ok);

    // ---- formatting ----
    static std::string codeGroups(const std::string &s,
                                   int groupSize = kDigitsPerGroup,
                                   int groupsPerLine = kGroupsPerLine,
                                   int linesPerPage = kLinesPerPage);

    // ---- Morse-cut substitution (off by default; see useMorseShorts()) ----
    std::string toMorseCut(const std::string &s) const;
    std::string fromMorseCut(const std::string &s) const;

    // ---- key-file I/O (UTF-8 on disk, same as otp.py/Quacque) ----
    std::string readFile(const std::string &fn, bool *ok);
    bool writeFile(const std::string &fn, const std::string &s);
    std::string loadKeyPad(const std::string &fn, bool *ok);
    std::string loadKeys(const std::vector<std::string> &keyFiles, bool *ok);
    bool wipeKeys(const std::vector<std::string> &keyFiles);
    bool wipeFile(const std::string &fn);

    // ---- key-join/unjoin support ----
    // The 252 five-of-ten row combinations used to expand two key sheets,
    // generated once in lexicographic order (0<1<2<3<4 style, digits 0-9) --
    // this order must exactly match otp.py's/Quacque's combo5x10 table,
    // since both sides of a join/unjoin must walk the same order to land on
    // the same checksum table.
    static const std::vector<std::string> &combo5x10();
    std::vector<std::string> combinateExpandedKeys(const std::string &keyInput, bool *ok);

    // ---- message split/merge support ----
    // General m-of-n combinations (unlike combo5x10() above, which is fixed
    // at 5-of-10 for join/unjoin): every m-subset of {1..n}, each returned as
    // the concatenation of its members' decimal digits (e.g. "123" for
    // {1,2,3}), in the same lexicographic order Python's
    // itertools.combinations() walks.
    static std::vector<std::string> combo(int m, int n);

    // Splits msgDigits into shareCount additive shares: shareCount-1 fresh
    // random strings plus msg minus their sum, so all shareCount shares are
    // needed to recover msgDigits (any shareCount-1 of them reveal nothing).
    std::vector<std::string> splitIntoShares(const std::string &msgDigits, int shareCount);
};
