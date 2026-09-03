#pragma once

// OTP -- core SilentDuck cipher engine, ported from otp.py.
//
// Full functional parity with otp.py's cipher/key operations: key generation
// ("-g"/"-gz"), encipher/decipher ("-e"/"-d"), key-join/unjoin ("-j"/"-u"),
// known-plaintext key recovery ("-f"), message split/merge ("-s"/"-m"),
// stream-combine ("-b"), and wipe ("-w"). otp.py's CLI argument parsing and
// help text are NOT here -- see OtpCli.h/.cpp, which is the console-facing
// layer built on top of this same public API (deliberately, so it exercises
// the identical surface a GUI will use). See ../quacque_design.md.
//
// Message text is passed in/out as QString rather than file paths -- the GUI
// owns how plaintext/ciphertext get in and out of the user's hands (a text
// field, clipboard, QR code, DTMF, whatever), not this class. Key material,
// by contrast, stays file-based (.otk files): keys are bulky, persistent, and
// meant to live on removable media (see the courier-mode / microSD notes in
// android_app/product_summary.md).
//
// otp.py calls die() (sys.exit) on any error, which suits a CLI but would
// crash a GUI app outright. Every failure path here instead returns false (or
// a null QString, check with .isNull()) and sets lastError() for the caller
// to show the user.
//
// NOTE: this file contains Cyrillic string literals (the straddling
// checkerboard's Cyrillic alphabet) and must be compiled with UTF-8 treated
// as the source encoding -- see the /utf-8 flag added for this target in
// CMakeLists.txt.

#include <QChar>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

class OTP
{
public:
    OTP();

    // ---- sheet / pad sizing -- mirrors otp.py's module-level constants ----
    static constexpr int kDigitsPerGroup = 5;
    static constexpr int kGroupsPerLine  = 5;
    static constexpr int kLinesPerPage   = 10;
    static constexpr int kSheetSize      = kDigitsPerGroup * kGroupsPerLine * kLinesPerPage; // 250
    static constexpr int kPagesPerPad    = 25;
    static constexpr int kPadSize        = kSheetSize * kPagesPerPad; // 6250

    // ---- configuration (mirrors otp.py's module-level globals) ----

    // If true, key files are left on disk after use instead of being wiped.
    // otp.py logs a loud warning in this case; do the same in the GUI.
    void setKeepKeyFilesAfterUse(bool keep) { m_keepKeyFilesAfterUse = keep; }
    bool keepKeyFilesAfterUse() const { return m_keepKeyFilesAfterUse; }

    // Number of overwrite passes wipeFile() performs before deleting a key
    // file. otp.py defaults to 7; each pass is forced to physical storage
    // via a real OS-level fsync/FlushFileBuffers, not just an app-level
    // flush() -- see forceFlushToDisk() in OTP.cpp.
    void setWipeRoundCount(int rounds) { m_wipeRoundCount = rounds; }
    int wipeRoundCount() const { return m_wipeRoundCount; }

    // When false (default/strict, matches otp.py), encode() requires an
    // explicit '#' digit-shift code around runs of digits and fails if one is
    // missing. Set true to have encode() insert the missing '#' automatically.
    void setAutoInsertDigitShiftCode(bool autoInsert) { m_autoInsertDigitShiftCode = autoInsert; }
    bool autoInsertDigitShiftCode() const { return m_autoInsertDigitShiftCode; }

    // otp.py's "-z" option: substitute Morse-cut letters for digits on the
    // wire. Off by default, matching otp.py.
    void setUseMorseShorts(bool use) { m_useMorseShorts = use; }
    bool useMorseShorts() const { return m_useMorseShorts; }

    // otp.py's "-t": dry-run mode. writeFile()/wipeFile() log what they
    // would have done and return success without touching the filesystem --
    // lets a command be rehearsed (e.g. checking key length against message
    // length) with zero risk of clobbering or deleting real files.
    void setTestingMode(bool testing) { m_testingMode = testing; }
    bool testingMode() const { return m_testingMode; }

    // otp.py's "-q": how many extra rounds of fresh random material get
    // folded into a freshly generated key sheet during keygen() (0 default =
    // one round). Higher values consume more entropy per sheet for extra
    // "whitening" -- see the long comment in otp.py this is ported from.
    void setRandomDuplicates(int n) { m_randomDuplicates = n; }
    int randomDuplicates() const { return m_randomDuplicates; }

    // otp.py's "-n": sleep this many seconds after every 25 fetched random
    // digits, giving the OS's entropy pool time to refill on constrained
    // hardware. 0 (default) disables it. NOTE: this blocks the calling
    // thread -- fine for a CLI, but a GUI must run key generation on a
    // worker thread before ever setting this above 0.
    void setEntropyGatheringSleepTime(int seconds) { m_entropyGatheringSleepTime = seconds; }
    int entropyGatheringSleepTime() const { return m_entropyGatheringSleepTime; }

    // True if ch belongs to the straddling checkerboard's alphabet: Roman
    // A-Z, the Cyrillic letters actually used by the checkerboard (see
    // buildTables()), digits 0-9, and the punctuation/control chars the
    // checkerboard maps (? : @ / ~ # . , space, newline). Case-insensitive,
    // matching encode()'s toUpper() normalization -- lowercase of any
    // allowed letter passes even though only the uppercase form is in the
    // table. Meant for gating keystrokes in an interactive text-entry UI
    // (e.g. TerminalEditor) before they ever reach encode(), not as a
    // substitute for encode()'s own validation.
    static bool isAllowedInputChar(QChar ch);

    // Message from the most recent call. Cleared at the start of every call
    // below. NOTE: a call can succeed (return true / a non-null QString) and
    // still leave a message here -- e.g. the crypto succeeded but wiping a
    // spent key file afterward failed. Always check this after every call,
    // not just after a failure, since a spent-key-wipe failure is a real
    // security-relevant thing the GUI should surface to the user.
    QString lastError() const { return m_lastError; }

    // ---- key generation ("-g" / "-gz") ----
    // Writes pagesPerPad .otk files named "<prefix>-NNN.otk" (NNN 001..pagesPerPad),
    // each one sheet (kSheetSize digits) of key material, grouped/lined the
    // same way otp.py's codeGroups() formats them. zeroKeys=true generates an
    // all-zero pad -- for testing only, NEVER for a real message.
    bool keygen(const QString &prefix, bool zeroKeys = false, int pagesPerPad = kPagesPerPad);

    // ---- encipher / decipher ("-e" / "-d") ----
    // keyFiles is one or more .otk paths, concatenated in order exactly as
    // otp.py's loadKeys() does. Spent key files are wiped (unless
    // keepKeyFilesAfterUse()) after a successful call. Returns a null
    // QString (.isNull() == true) on failure -- check lastError().
    QString encipher(const QString &plainText, const QStringList &keyFiles);
    QString decipher(const QString &cipherText, const QStringList &keyFiles);

    // ---- key join / unjoin ("-j" / "-u") ----
    // Combine two kSheetSize-digit key sheets (fileI, fileJ) into a fresh
    // 25-sheet keypad. combinedKeyFile is the in-the-clear table exchanged
    // between the join side and the unjoin side so both derive the same new
    // keypad without ever putting the new keypad itself on the wire.
    // fileI/fileJ are wiped after use (unless keepKeyFilesAfterUse()).
    //
    // NOTE (found while porting, not fixed here -- see quacque_design.md):
    // otp.py names the 25 output sheets differently between the two sides --
    // join writes "<prefix>NN.otk" (no dash) while unjoin writes
    // "<prefix>-NN.otk" (with a dash). That asymmetry is reproduced exactly
    // below for behavioral parity with otp.py; it should be resolved at the
    // source before this ships.
    bool joinKeys(const QString &fileI, const QString &fileJ,
                  const QString &combinedKeyFile, const QString &prefix);
    bool unjoinKeys(const QString &fileI, const QString &fileJ,
                     const QString &combinedKeyFile, const QString &prefix);

    // ---- known-plaintext key recovery ("-f") ----
    // Given a plaintext message and the ciphertext it's known to correspond
    // to, computes the key that would produce that exact pairing and writes
    // it to keyOutputFile. Deliberate SilentDuck/otp.py feature, not a bug --
    // see otp.py's do_fakeMsg() for the rationale (repudiation: any OTP
    // ciphertext can be "decrypted" to any plaintext of the same length,
    // given a manufactured key to match).
    bool generateKeyForKnownPlaintext(const QString &plainText, const QString &cipherText,
                                       const QString &keyOutputFile);

    // ---- message split ("-s") ----
    // Splits plainText into maxParts numbered shares such that any minParts
    // of them reconstruct the message -- a simple additive/subtractive
    // scheme, not OTP-keyed (no key file involved). Writes
    // "<filePrefix><courier>-<group>.otp" for every courier in every
    // minParts-of-maxParts combination group; see otp.py's do_splitMsg() for
    // the courier-distribution rationale.
    bool splitMessage(const QString &plainText, int minParts, int maxParts,
                       const QString &filePrefix);

    // ---- message merge ("-m") ----
    // Inverse of splitMessage(): reconstructs the plaintext from at least
    // minParts of the share files written above (order doesn't matter, but
    // they must all belong to the same courier group). Segment files are
    // wiped after use (unless keepKeyFilesAfterUse()). Returns a null
    // QString on failure -- check lastError().
    QString mergeMessage(const QStringList &segmentFiles);

    // ---- stream combine ("-b") ----
    // Adds two equal-length digit streams together (digit-wise, mod 10, no
    // carry) -- a simple way to combine entropy from two separate key
    // sources into one. combinedFile and/or keyPrefix may be empty to skip
    // that output, matching otp.py's do_combineStreams(); when keyPrefix is
    // given, the combined stream is also sliced into kSheetSize-digit sheets
    // named "<keyPrefix>-NN.otk". inputFile1/inputFile2 are wiped after use
    // (unless keepKeyFilesAfterUse()).
    bool combineStreams(const QString &inputFile1, const QString &inputFile2,
                         const QString &combinedFile, const QString &keyPrefix);

    // ---- wipe ("-w") ----
    // Securely wipes an arbitrary list of files, same overwrite-then-delete
    // logic as the internal key-wipe path. Unlike key wiping elsewhere, this
    // always runs -- it's an explicit user request, not automatic
    // housekeeping, so it's not gated by keepKeyFilesAfterUse().
    bool wipeFiles(const QStringList &files);

private:
    // ---- config state ----
    bool m_keepKeyFilesAfterUse = false;
    int  m_wipeRoundCount = 7;
    bool m_autoInsertDigitShiftCode = false;
    bool m_useMorseShorts = false;
    bool m_testingMode = false;
    int  m_randomDuplicates = 0;
    int  m_entropyGatheringSleepTime = 0;
    int  m_fetchedEntropyCount = 0; // digits fetched since the last entropySleep()
    static constexpr int kFetchedEntropyQuota = 25; // matches otp.py's fetchedEntropyQuota
    mutable QString m_lastError;

    void clearError() { m_lastError.clear(); }
    void setError(const QString &msg) { m_lastError = msg; }

    // ---- straddling-checkerboard tables (built once, in the constructor) ----
    QMap<QString, QChar> m_number2lat;
    QMap<QChar, QString> m_lat2number;
    QMap<QString, QChar> m_number2cyr;
    QMap<QChar, QString> m_cyr2number;
    QString m_validChars;
    void buildTables();

    // Single source of truth for m_validChars above and for the public
    // isAllowedInputChar() -- kept as one function so the two can't drift
    // out of sync with each other or with buildTables()'s tables.
    static QString allowedInputChars();

    // ---- low-level digit-string arithmetic (add/subtract per-digit, mod 10,
    // no carry -- exactly otp.py's stringAdd()/stringSubtract()) ----
    QString stringAddDigits(const QString &a, const QString &b, bool *ok);
    QString stringSubtractDigits(const QString &a, const QString &b, bool *ok);
    static QString stringDigits(const QString &s);
    bool stringValid(const QString &s) const;

    // ---- randomness ----
    // NOTE: uses QRandomGenerator::system(), Qt's wrapper over the OS CSPRNG
    // (CryptGenRandom/BCrypt on Windows, /dev/urandom-equivalent elsewhere) --
    // the same security tier as otp.py's os.urandom(), no better, no worse.
    // If/when a stronger, audited guarantee is wanted, swap this for
    // libsodium's randombytes_buf() -- see quacque_design.md.
    //
    // Not static (unlike the rest of this section) because they consult and
    // update m_fetchedEntropyCount via entropySleep() -- see
    // setEntropyGatheringSleepTime()'s doc comment above.
    QChar randDigit();
    QString randDigits(int count);
    void entropySleep();

    // ---- checkerboard encode/decode ----
    QString insertAlphabetSwitches(const QString &s) const;
    QString encode(const QString &plain, bool *ok);
    QString decode(const QString &digits, bool *ok);

    // ---- formatting ----
    static QString codeGroups(const QString &s,
                               int groupSize = kDigitsPerGroup,
                               int groupsPerLine = kGroupsPerLine,
                               int linesPerPage = kLinesPerPage);

    // ---- Morse-cut substitution (off by default; see useMorseShorts()) ----
    QString toMorseCut(const QString &s) const;
    QString fromMorseCut(const QString &s) const;

    // ---- key-file I/O ----
    QString readFile(const QString &fn, bool *ok);
    bool writeFile(const QString &fn, const QString &s);
    QString loadKeyPad(const QString &fn, bool *ok);
    QString loadKeys(const QStringList &keyFiles, bool *ok);
    bool wipeKeys(const QStringList &keyFiles);
    bool wipeFile(const QString &fn);

    // ---- key-join/unjoin support ----
    // The 252 five-of-ten row combinations used to expand two key sheets,
    // generated once in lexicographic order (0<1<2<3<4 style, digits 0-9) --
    // this order must exactly match otp.py's combo5x10 table, since both
    // sides of a join/unjoin must walk the same order to land on the same
    // checksum table.
    static const QVector<QString> &combo5x10();
    QVector<QString> combinateExpandedKeys(const QString &keyInput, bool *ok);

    // ---- message split/merge support ----
    // General m-of-n combinations (unlike combo5x10() above, which is
    // fixed at 5-of-10 for join/unjoin): every m-subset of {1..n}, each
    // returned as the concatenation of its members' decimal digits (e.g.
    // "123" for {1,2,3}), in the same lexicographic order Python's
    // itertools.combinations() walks -- mirrors otp.py's combo(m, n).
    static QVector<QString> combo(int m, int n);

    // Splits msgDigits into shareCount additive shares -- otp.py's
    // doMsgSplit(): shareCount-1 fresh random strings plus msg minus their
    // sum, so all shareCount shares are needed to recover msgDigits (any
    // shareCount-1 of them reveal nothing).
    QVector<QString> splitIntoShares(const QString &msgDigits, int shareCount);
};
