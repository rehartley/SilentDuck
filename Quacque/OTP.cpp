#include "OTP.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRandomGenerator>
#include <QTextStream>
#include <QThread>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

// Force an OS-level flush of fd's buffered writes to physical storage --
// Qt/CRT-level flush() alone only moves data from the app's buffers to the
// OS's, not from the OS's page cache to the device. Used after every wipe
// pass in wipeFile() below and after writeFile()'s single write, mirroring
// what otp.py's os.fsync() calls were for (otp.py calls it three times per
// wipe pass; that was almost certainly just paranoid redundancy -- a single
// fsync/FlushFileBuffers already blocks until the data is durably written,
// so one real call here is more rigorous than three Python-level calls that
// all resolve to the same one syscall).
static void forceFlushToDisk(int fd)
{
#ifdef Q_OS_WIN
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h != INVALID_HANDLE_VALUE)
        FlushFileBuffers(h);
#else
    fsync(fd);
#endif
}

OTP::OTP()
{
    buildTables();
}

// ============================================================================
// straddling-checkerboard tables
// ============================================================================

void OTP::buildTables()
{
    // number -> Roman letter (single digits, then double digits 70-99)
    m_number2lat = {
        {"0", QChar('A')}, {"1", QChar('E')}, {"2", QChar('N')}, {"3", QChar('R')},
        {"4", QChar('O')}, {"5", QChar('I')}, {"6", QChar('T')},
        {"70", QChar('B')}, {"71", QChar('C')}, {"72", QChar('G')}, {"73", QChar('D')}, {"74", QChar('F')},
        {"75", QChar('H')}, {"76", QChar('J')}, {"77", QChar('K')}, {"78", QChar('L')}, {"79", QChar('M')},
        {"80", QChar('P')}, {"81", QChar('Q')}, {"82", QChar('S')}, {"83", QChar('U')}, {"84", QChar('V')},
        {"85", QChar('W')}, {"86", QChar('X')}, {"87", QChar('Y')}, {"88", QChar('Z')},
        // 89 intentionally unassigned -- see otp.py's comment: reserved for a
        // raw-code-plus-length scheme that was never implemented.
        {"90", QChar('?')}, {"91", QChar(':')}, {"92", QChar('@')}, {"93", QChar('/')},
        {"94", QChar('#')}, {"95", QChar('.')}, {"96", QChar(',')}, {"97", QChar('\n')},
        {"98", QChar(' ')}, {"99", QChar('~')},
    };

    // number -> Cyrillic letter
    // Cyrillic code points given as numeric \u escapes (А=0x0410, Е=0x0415,
    // И=0x0418, Н=0x041D, О=0x041E, С=0x0421, Т=0x0422, Б=0x0411, В=0x0412,
    // Г=0x0413, Д=0x0414, Ж=0x0416, З=0x0417, Й=0x0419, К=0x041A, Л=0x041B,
    // М=0x041C, П=0x041F, Р=0x0420, У=0x0423, Ф=0x0424, Х=0x0425, Ц=0x0426,
    // Ч=0x0427, Ш=0x0428, Щ=0x0429, Ы=0x042B, Ь=0x042C, Э=0x042D, Ю=0x042E,
    // Я=0x042F) rather than embedded literal characters, so this table can't
    // silently corrupt if a future edit/diff/clone mangles the source file's
    // encoding -- that's the one thing in this file that would break without
    // an obvious build error.
    m_number2cyr = {
        {"0", QChar(0x0410)}, {"1", QChar(0x0415)}, {"2", QChar(0x0418)}, {"3", QChar(0x041D)},
        {"4", QChar(0x041E)}, {"5", QChar(0x0421)}, {"6", QChar(0x0422)},
        {"70", QChar(0x0411)}, {"71", QChar(0x0412)}, {"72", QChar(0x0413)}, {"73", QChar(0x0414)}, {"74", QChar(0x0416)},
        {"75", QChar(0x0417)}, {"76", QChar(0x0419)}, {"77", QChar(0x041A)}, {"78", QChar(0x041B)}, {"79", QChar(0x041C)},
        {"80", QChar(0x041F)}, {"81", QChar(0x0420)}, {"82", QChar(0x0423)}, {"83", QChar(0x0424)}, {"84", QChar(0x0425)},
        {"85", QChar(0x0426)}, {"86", QChar(0x0427)}, {"87", QChar(0x0428)}, {"88", QChar(0x0429)}, {"89", QChar(0x042B)},
        {"90", QChar(0x042C)}, {"91", QChar(0x042D)}, {"92", QChar(0x042E)}, {"93", QChar(0x042F)},
        {"94", QChar('#')}, {"95", QChar('.')}, {"96", QChar(',')}, {"97", QChar('\n')},
        {"98", QChar(' ')}, {"99", QChar('~')},
    };

    m_lat2number.clear();
    for (auto it = m_number2lat.cbegin(); it != m_number2lat.cend(); ++it)
        m_lat2number[it.value()] = it.key();

    m_cyr2number.clear();
    for (auto it = m_number2cyr.cbegin(); it != m_number2cyr.cend(); ++it)
        m_cyr2number[it.value()] = it.key();

    // otp.py's validStr: Roman letters, punctuation/control chars, digits,
    // then the Cyrillic alphabet.
    m_validChars = allowedInputChars();
}

QString OTP::allowedInputChars()
{
    QString chars = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ?:@/~#., \n0123456789");
    // The Cyrillic letters used by m_number2cyr above, spelled out via \u
    // escapes for the same file-corruption-resistance reason documented in
    // buildTables()'s comment. Kept as a plain literal (not derived from
    // m_number2cyr) so this stays callable without an OTP instance.
    static const char16_t cyrillic[] = {
        0x0410, 0x0415, 0x0418, 0x041D, 0x041E, 0x0421, 0x0422,
        0x0411, 0x0412, 0x0413, 0x0414, 0x0416, 0x0417, 0x0419, 0x041A, 0x041B, 0x041C,
        0x041F, 0x0420, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042B,
        0x042C, 0x042D, 0x042E, 0x042F,
    };
    for (char16_t cp : cyrillic)
        chars += QChar(cp);
    return chars;
}

bool OTP::isAllowedInputChar(QChar ch)
{
    static const QString chars = allowedInputChars();
    return chars.contains(ch.toUpper());
}

bool OTP::stringValid(const QString &s) const
{
    const QString upper = s.toUpper();
    for (const QChar &ch : upper) {
        if (!m_validChars.contains(ch))
            return false;
    }
    return true;
}

// ============================================================================
// digit-string arithmetic
// ============================================================================

QString OTP::stringAddDigits(const QString &a, const QString &b, bool *ok)
{
    if (ok) *ok = true;
    if (a.length() > b.length()) {
        setError(QStringLiteral("stringAdd: len(a) > len(b) by %1 characters").arg(a.length() - b.length()));
        if (ok) *ok = false;
        return QString();
    }
    QString tmp;
    tmp.reserve(a.length());
    for (int x = 0; x < a.length(); ++x) {
        int d = a[x].digitValue() + b[x].digitValue();
        if (d > 9) d -= 10;
        tmp += QChar(u'0' + d);
    }
    return tmp;
}

QString OTP::stringSubtractDigits(const QString &a, const QString &b, bool *ok)
{
    if (ok) *ok = true;
    if (a.length() > b.length()) {
        setError(QStringLiteral("stringSubtract: len(a) > len(b) by %1 characters").arg(a.length() - b.length()));
        if (ok) *ok = false;
        return QString();
    }
    QString tmp;
    tmp.reserve(a.length());
    for (int x = 0; x < a.length(); ++x) {
        int d = a[x].digitValue() - b[x].digitValue();
        if (d < 0) d += 10;
        tmp += QChar(u'0' + d);
    }
    return tmp;
}

QString OTP::stringDigits(const QString &s)
{
    QString tmp;
    tmp.reserve(s.length());
    for (const QChar &ch : s) {
        if (ch.isDigit())
            tmp += ch;
    }
    return tmp;
}

// ============================================================================
// randomness
// ============================================================================

void OTP::entropySleep()
{
    // otp.py's entropySleep(): optionally sleep for a bit every
    // kFetchedEntropyQuota digits, to give the OS's entropy pool time to
    // refill on constrained hardware. A no-op unless
    // setEntropyGatheringSleepTime() has been called -- and even then, this
    // blocks whatever thread calls randDigit(), which is fine for a CLI but
    // is the caller's responsibility to keep off a GUI's main thread.
    if (m_entropyGatheringSleepTime <= 0)
        return;

    ++m_fetchedEntropyCount;
    if (m_fetchedEntropyCount > kFetchedEntropyQuota) {
        QThread::sleep(m_entropyGatheringSleepTime);
        m_fetchedEntropyCount = 0;
    }
}

QChar OTP::randDigit()
{
    entropySleep();
    // QRandomGenerator::bounded() does its own unbiased rejection sampling
    // internally -- this is the C++ equivalent of otp.py's manual
    // os.urandom(1)-and-retry loop, not a byte-for-byte replica of it (the
    // actual random values can never match across two independent CSPRNGs
    // anyway; what has to match is "uniformly distributed digit, no modulo
    // bias", and both approaches give that).
    return QChar(u'0' + QRandomGenerator::system()->bounded(10));
}

QString OTP::randDigits(int count)
{
    QString tmp;
    tmp.reserve(count);
    for (int i = 0; i < count; ++i)
        tmp += randDigit();
    return tmp;
}

// ============================================================================
// formatting
// ============================================================================

QString OTP::codeGroups(const QString &s, int groupSize, int groupsPerLine, int linesPerPage)
{
    QString tmp;
    int lineCount = 0;
    int groupCount = 0;

    for (int x = 0; x < s.length(); x += groupSize) {
        tmp += s.mid(x, groupSize) + QLatin1Char(' ');
        ++groupCount;

        if (groupCount >= groupsPerLine) {
            tmp += QLatin1Char('\n');
            groupCount = 0;
            ++lineCount;

            if (lineCount >= linesPerPage) {
                tmp += QLatin1Char('\n');
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
const QMap<QChar, QChar> &morseCutLetter()
{
    static const QMap<QChar, QChar> tbl = {
        {QChar('0'), QChar('T')}, {QChar('1'), QChar('A')}, {QChar('2'), QChar('U')},
        {QChar('3'), QChar('V')}, {QChar('4'), QChar('4')}, {QChar('5'), QChar('E')},
        {QChar('6'), QChar('6')}, {QChar('7'), QChar('B')}, {QChar('8'), QChar('D')},
        {QChar('9'), QChar('N')},
    };
    return tbl;
}
const QMap<QChar, QChar> &morseCutNumber()
{
    static const QMap<QChar, QChar> tbl = {
        {QChar('T'), QChar('0')}, {QChar('A'), QChar('1')}, {QChar('U'), QChar('2')},
        {QChar('V'), QChar('3')}, {QChar('4'), QChar('4')}, {QChar('E'), QChar('5')},
        {QChar('6'), QChar('6')}, {QChar('B'), QChar('7')}, {QChar('D'), QChar('8')},
        {QChar('N'), QChar('9')},
    };
    return tbl;
}
} // namespace

QString OTP::toMorseCut(const QString &s) const
{
    if (!m_useMorseShorts)
        return s;
    QString tmp;
    for (const QChar &ch : s) {
        if (ch == QChar(' ') || ch == QChar('\n'))
            continue;
        tmp += morseCutLetter().value(ch, QChar('?'));
    }
    return tmp;
}

QString OTP::fromMorseCut(const QString &s) const
{
    if (!m_useMorseShorts)
        return s;
    const QString upper = s.toUpper();
    QString tmp;
    for (const QChar &ch : upper) {
        if (ch == QChar(' ') || ch == QChar('\n'))
            continue;
        tmp += morseCutNumber().value(ch, QChar('?'));
    }
    return tmp;
}

// ============================================================================
// checkerboard encode/decode -- ported as a near-literal transliteration of
// otp.py's index arithmetic (see decode() especially): this is exactly the
// kind of code where a "cleaner" rewrite risks a subtle off-by-one, so the
// same control flow/increments are kept rather than restructured.
// ============================================================================

QString OTP::insertAlphabetSwitches(const QString &s) const
{
    QString tmp;
    bool usingRoman = true;

    for (const QChar &ch : s) {
        if (ch == QChar('~')) {
            usingRoman = !usingRoman;
            tmp += ch;
            continue;
        }

        const bool inLat = m_lat2number.contains(ch);
        const bool inCyr = m_cyr2number.contains(ch);

        if (inLat && !inCyr && !usingRoman) {
            tmp += QChar('~');
            usingRoman = true;
        } else if (inCyr && !inLat && usingRoman) {
            tmp += QChar('~');
            usingRoman = false;
        }

        tmp += ch;
    }
    return tmp;
}

QString OTP::encode(const QString &plain, bool *ok)
{
    if (ok) *ok = true;

    bool usingRoman = true;
    bool usingDigits = false;
    QString s = plain.toUpper();

    if (!stringValid(s)) {
        setError(QStringLiteral("invalid characters in string: %1").arg(s));
        if (ok) *ok = false;
        return QString();
    }

    s = insertAlphabetSwitches(s);
    const int lenS = s.length();
    QString tmp;

    const QMap<QChar, QString> *currentTbl = &m_lat2number;

    int x = 0;
    while (x < lenS) {
        const QChar ch = s[x];

        if (ch == QChar('#')) {
            usingDigits = !usingDigits;
            tmp += QStringLiteral("94");
        } else if (ch == QChar('~')) {
            tmp += QStringLiteral("99");
            usingRoman = !usingRoman;
            currentTbl = usingRoman ? &m_lat2number : &m_cyr2number;
        } else if (ch.isDigit()) {
            if (!usingDigits) {
                if (m_autoInsertDigitShiftCode) {
                    usingDigits = true;
                    tmp += QStringLiteral("94");
                } else {
                    setError(QStringLiteral("missing digit code '#' at start of number sequence"));
                    if (ok) *ok = false;
                    return QString();
                }
            }
            tmp += ch;
            tmp += ch;
        } else {
            if (usingDigits) {
                if (m_autoInsertDigitShiftCode) {
                    usingDigits = false;
                    tmp += QStringLiteral("94");
                } else {
                    setError(QStringLiteral("missing '#' code to end number sequence"));
                    if (ok) *ok = false;
                    return QString();
                }
            }

            if (!currentTbl->contains(ch)) {
                const QString alphabetName = usingRoman ? QStringLiteral("Roman") : QStringLiteral("Cyrillic");
                setError(QStringLiteral("letter '%1' not in the %2 alphabet - missing '~'?").arg(ch).arg(alphabetName));
                if (ok) *ok = false;
                return QString();
            }
            tmp += currentTbl->value(ch);
        }
        ++x;
    }

    if (usingDigits) {
        if (m_autoInsertDigitShiftCode) {
            tmp += QStringLiteral("94");
        } else {
            setError(QStringLiteral("missing final '#' code to end number sequence"));
            if (ok) *ok = false;
            return QString();
        }
    }

    if (!usingRoman)
        tmp += QStringLiteral("99");

    return tmp;
}

QString OTP::decode(const QString &digits, bool *ok)
{
    if (ok) *ok = true;

    const QString s = stringDigits(digits);
    const int lenS = s.length();
    bool usingRoman = true;
    QString tmp;
    QString code;
    int x = 0;

    const QMap<QString, QChar> *number2letter = &m_number2lat;

    while (x < lenS) {
        code = s[x];
        if (code > QStringLiteral("6")) {
            // double-digit code
            ++x;
            if (x < lenS) {
                code += s[x];
            } else {
                setError(QStringLiteral("string missing a character while decoding: %1").arg(digits));
                if (ok) *ok = false;
                return QString();
            }
        }

        if (code == QStringLiteral("94")) {
            // '#' is a control code, not part of the message -- tracked here
            // but not written to the output, same as '99' below, so decoded
            // text comes back clean without the caller having to strip it.
            ++x;
            code.clear();
            while (code != QStringLiteral("94")) {
                if (lenS < x + 2) {
                    setError(QStringLiteral("numbers are short a digit: %1").arg(digits));
                    if (ok) *ok = false;
                    return QString();
                }
                code = s.mid(x, 2);

                static const QStringList numberCodes = {
                    "00", "11", "22", "33", "44", "55", "66", "77", "88", "99"
                };
                if (numberCodes.contains(code)) {
                    tmp += code[0];
                } else if (code == QStringLiteral("94")) {
                    x -= 1;
                } else {
                    setError(QStringLiteral("error decoding digit run: %1").arg(digits));
                    if (ok) *ok = false;
                    return QString();
                }
                x += 2;
            }
        } else if (code == QStringLiteral("99")) {
            usingRoman = !usingRoman;
            number2letter = usingRoman ? &m_number2lat : &m_number2cyr;
        } else {
            if (!number2letter->contains(code)) {
                setError(QStringLiteral("code '%1' is not assigned to any letter in this alphabet: %2").arg(code, digits));
                if (ok) *ok = false;
                return QString();
            }
            tmp += number2letter->value(code);
        }

        ++x;
    }

    return tmp;
}

// ============================================================================
// key-file I/O
// ============================================================================

QString OTP::readFile(const QString &fn, bool *ok)
{
    if (ok) *ok = true;
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QStringLiteral("error reading file: %1").arg(fn));
        if (ok) *ok = false;
        return QString();
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString contents = in.readAll();
    f.close();
    return contents;
}

bool OTP::writeFile(const QString &fn, const QString &s)
{
    if (m_testingMode) {
        setError(QStringLiteral("TESTING MODE: writeFile() did not write: %1").arg(fn));
        return true; // logged via lastError(), not a failure -- matches otp.py's dbg()-and-return
    }

    const QFileInfo fi(fn);
    const QString parentDir = fi.path();
    if (!parentDir.isEmpty() && !QDir(parentDir).exists()) {
        if (!QDir().mkpath(parentDir)) {
            setError(QStringLiteral("error creating directory: %1").arg(parentDir));
            return false;
        }
    }

    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setError(QStringLiteral("error writing file: %1").arg(fn));
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << s;
    out.flush();
    f.flush();
    forceFlushToDisk(f.handle());
    f.close();
    return true;
}

QString OTP::loadKeyPad(const QString &fn, bool *ok)
{
    const QString raw = readFile(fn, ok);
    if (ok && !*ok)
        return QString();
    return stringDigits(raw);
}

QString OTP::loadKeys(const QStringList &keyFiles, bool *ok)
{
    if (ok) *ok = true;
    QString key;
    for (const QString &kfn : keyFiles) {
        bool readOk = true;
        key += readFile(kfn, &readOk);
        if (!readOk) {
            if (ok) *ok = false;
            return QString();
        }
    }
    return stringDigits(key);
}

bool OTP::wipeFile(const QString &fn)
{
    if (m_testingMode) {
        setError(QStringLiteral("TESTING MODE: wipeFile() did not execute: %1").arg(fn));
        return true;
    }

    QFileInfo fi(fn);
    const qint64 fs = fi.size();

    QFile f(fn);
    if (!f.open(QIODevice::ReadWrite)) {
        setError(QStringLiteral("error opening file to wipe: %1").arg(fn));
        return false;
    }

    // all ones, all zeroes, a spread of hex patterns, then zeroes again --
    // same pattern list as otp.py's wipeFile().
    static const quint8 patterns[] = {
        0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
    };

    for (int r = 0; r < m_wipeRoundCount; ++r) {
        for (quint8 byteVal : patterns) {
            const QByteArray pattern(static_cast<int>(fs), static_cast<char>(byteVal));
            f.seek(0);
            f.write(pattern);
            f.flush();
            forceFlushToDisk(f.handle());
        }
    }
    f.close();

    if (!QFile::remove(fn)) {
        setError(QStringLiteral("error removing wiped file: %1").arg(fn));
        return false;
    }
    return true;
}

bool OTP::wipeKeys(const QStringList &keyFiles)
{
    if (m_keepKeyFilesAfterUse) {
        setError(QStringLiteral("warning: key files kept after use (keepKeyFilesAfterUse is set)"));
        return true;
    }

    // Unlike otp.py (which die()s the whole process on the first wipe
    // failure, potentially leaving later key files un-wiped), keep going
    // through every file and report the last failure -- more of the spent
    // keys end up actually wiped this way, which matters more than which
    // exact error message survives.
    bool allOk = true;
    for (const QString &kfn : keyFiles) {
        if (!wipeFile(kfn))
            allOk = false;
    }
    return allOk;
}

// ============================================================================
// key generation
// ============================================================================

bool OTP::keygen(const QString &prefix, bool zeroKeys, int pagesPerPad)
{
    clearError();
    if (prefix.isEmpty()) {
        setError(QStringLiteral("no key prefix given"));
        return false;
    }

    for (int x = 1; x <= pagesPerPad; ++x) {
        const QString fn = QStringLiteral("%1-%2.otk").arg(prefix).arg(x, 3, 10, QChar('0'));

        QString tmp;
        if (zeroKeys) {
            tmp = QString(kSheetSize, QChar('0'));
        } else {
            // otp.py folds (1 + m_randomDuplicates) fresh random draws
            // together via stringAdd() rather than taking just one --
            // m_randomDuplicates defaults to 0, so this is one draw unless
            // the caller (via "-q") asked for extra whitening rounds.
            tmp = QString(kSheetSize, QChar('0'));
            bool ok = true;
            for (int i = 0; i < 1 + m_randomDuplicates; ++i) {
                tmp = stringAddDigits(tmp, randDigits(kSheetSize), &ok);
                if (!ok)
                    return false;
            }
        }

        if (!writeFile(fn, codeGroups(tmp)))
            return false;
    }
    return true;
}

// ============================================================================
// encipher / decipher
// ============================================================================

QString OTP::encipher(const QString &plainText, const QStringList &keyFiles)
{
    clearError();

    bool ok = true;
    const QString key = loadKeys(keyFiles, &ok);
    if (!ok)
        return QString();

    const QString inputTxt = plainText.toUpper();
    const QString encodedTxt = encode(inputTxt, &ok);
    if (!ok)
        return QString();

    if (encodedTxt.length() > key.length()) {
        setError(QStringLiteral("not enough key material: message needs %1 digits, key has %2")
                     .arg(encodedTxt.length()).arg(key.length()));
        return QString();
    }

    // Deliberately encodedTxt - key (not key - encodedTxt): this is what
    // lets a message prefixed with "AAAAA" (-> "00000") double as an
    // easy-to-eyeball confirmation of which keypad was used -- see the
    // long comment in otp.py's do_encipher().
    const QString cipherTxt = stringSubtractDigits(encodedTxt, key, &ok);
    if (!ok)
        return QString();

    const QString result = codeGroups(toMorseCut(cipherTxt));

    // Crypto succeeded -- wipe the spent keys, but a wipe failure is a
    // secondary (if security-relevant) concern, not a reason to withhold the
    // already-correct ciphertext. See lastError()'s doc comment.
    QString cryptoError; // empty unless wipe fails below
    if (!wipeKeys(keyFiles))
        cryptoError = m_lastError;

    if (!cryptoError.isEmpty())
        setError(cryptoError);
    else
        clearError();

    return result;
}

QString OTP::decipher(const QString &cipherText, const QStringList &keyFiles)
{
    clearError();

    bool ok = true;
    const QString key = loadKeys(keyFiles, &ok);
    if (!ok)
        return QString();

    QString inputTxt = fromMorseCut(cipherText);
    inputTxt = stringDigits(inputTxt);

    if (inputTxt.length() > key.length()) {
        setError(QStringLiteral("not enough key material: ciphertext needs %1 digits, key has %2")
                     .arg(inputTxt.length()).arg(key.length()));
        return QString();
    }

    const QString clearTxt = stringAddDigits(inputTxt, key, &ok);
    if (!ok)
        return QString();

    const QString decodedTxt = decode(clearTxt, &ok);
    if (!ok)
        return QString();

    QString cryptoError;
    if (!wipeKeys(keyFiles))
        cryptoError = m_lastError;

    if (!cryptoError.isEmpty())
        setError(cryptoError);
    else
        clearError();

    return decodedTxt;
}

// ============================================================================
// key join / unjoin
// ============================================================================

const QVector<QString> &OTP::combo5x10()
{
    // All 5-of-10 combinations of digits 0-9, in lexicographic order --
    // "01234", "01235", ..., "56789" -- 252 entries. Generated once here
    // rather than transcribed as a 252-entry literal table (otp.py hardcodes
    // it as a literal list); this generates the identical order because it's
    // the same lexicographic walk Python's itertools.combinations() takes.
    static const QVector<QString> table = [] {
        QVector<QString> t;
        t.reserve(252);
        for (int a = 0; a <= 5; ++a)
        for (int b = a + 1; b <= 6; ++b)
        for (int c = b + 1; c <= 7; ++c)
        for (int d = c + 1; d <= 8; ++d)
        for (int e = d + 1; e <= 9; ++e) {
            QString combo;
            combo += QChar(u'0' + a);
            combo += QChar(u'0' + b);
            combo += QChar(u'0' + c);
            combo += QChar(u'0' + d);
            combo += QChar(u'0' + e);
            t.append(combo);
        }
        return t;
    }();
    return table;
}

QVector<QString> OTP::combinateExpandedKeys(const QString &keyInput, bool *ok)
{
    if (ok) *ok = true;

    QString rows[10];
    for (int d = 0; d < 10; ++d)
        rows[d] = keyInput.mid(d * 25, 25);

    QVector<QString> retVal;
    retVal.reserve(252);

    for (const QString &combo : combo5x10()) {
        QString s(25, QChar('0'));
        bool subOk = true;
        for (const QChar &digitCh : combo) {
            s = stringSubtractDigits(s, rows[digitCh.digitValue()], &subOk);
            if (!subOk) {
                if (ok) *ok = false;
                return {};
            }
        }

        bool addOk = true;
        QString checkSum = s.mid(0, 5);
        checkSum = stringAddDigits(checkSum, s.mid(5, 5), &addOk);
        checkSum = stringAddDigits(checkSum, s.mid(10, 5), &addOk);
        checkSum = stringAddDigits(checkSum, s.mid(15, 5), &addOk);
        checkSum = stringAddDigits(checkSum, s.mid(20, 5), &addOk);
        if (!addOk) {
            if (ok) *ok = false;
            return {};
        }

        retVal.append(s + checkSum);
    }
    return retVal;
}

// joinKeys() and unjoinKeys() share everything up through recovering the
// 504-row, checksum-sorted, halved key table -- identical in both
// directions (this mirrors otp.py, which duplicates the same block in both
// functions with a "copied from above, though it is not the same -- be
// careful" comment, rather than factoring it out). Only what happens with
// the two halves afterward differs: join masks them with fresh randomness
// and emits both; unjoin recovers that same randomness from an
// already-received combinedKeyFile.

bool OTP::joinKeys(const QString &fileI, const QString &fileJ,
                    const QString &combinedKeyFile, const QString &prefix)
{
    clearError();
    if (prefix.isEmpty()) {
        setError(QStringLiteral("no key prefix given"));
        return false;
    }

    bool ok = true;
    const QString ki = loadKeyPad(fileI, &ok);
    if (!ok || ki.length() != kSheetSize) {
        setError(QStringLiteral("bad first key (expected %1 digits, got %2)").arg(kSheetSize).arg(ki.length()));
        return false;
    }
    const QString kj = loadKeyPad(fileJ, &ok);
    if (!ok || kj.length() != kSheetSize) {
        setError(QStringLiteral("bad second key (expected %1 digits, got %2)").arg(kSheetSize).arg(kj.length()));
        return false;
    }

    const QVector<QString> iTmp = combinateExpandedKeys(ki, &ok);
    if (!ok) return false;
    const QVector<QString> jTmp = combinateExpandedKeys(kj, &ok);
    if (!ok) return false;

    QVector<QString> tmp = iTmp;
    tmp += jTmp; // 504 rows of 30 digits (25 + 5 checksum)

    // index by the trailing 5-digit checksum, resolving collisions by
    // walking forward mod 100000 -- must match unjoinKeys()'s walk exactly.
    QMap<QString, QString> keys;
    for (const QString &row : tmp) {
        QString checkSum = row.right(5);
        const QString value = row.left(row.length() - 5);

        while (keys.contains(checkSum)) {
            const int csNum = (checkSum.toInt() + 1) % 100000;
            checkSum = QStringLiteral("%1").arg(csNum, 5, 10, QChar('0'));
        }
        keys[checkSum] = value;
    }

    // QMap keeps keys sorted already; values() in key order == the sorted
    // walk Python does explicitly with sorted(keys.keys()).
    // QMap iterates/values() in ascending key order already, which is
    // exactly Python's explicit "for k in sorted(keys.keys())" walk.
    const QList<QString> sortedValsList = keys.values();
    const QVector<QString> sortedVals(sortedValsList.cbegin(), sortedValsList.cend());
    const int half = sortedVals.size() / 2;
    const QVector<QString> keysA = sortedVals.mid(0, half);
    const QVector<QString> keysB = sortedVals.mid(half);

    QString newOtpTbl;
    QString newRandomKeyStr;
    newOtpTbl.reserve(half * 25);
    newRandomKeyStr.reserve(half * 25);

    for (int x = 0; x < half; ++x) {
        QString tmp_s = stringAddDigits(keysA[x], keysB[x], &ok);
        if (!ok) return false;

        // freshly generated random pad material -- this, not the ciphertext
        // below, is the new keypad delivered locally in the clear further
        // down.
        const QString rd = randDigits(25);
        newRandomKeyStr += rd;

        tmp_s = stringSubtractDigits(tmp_s, rd, &ok);
        if (!ok) return false;
        newOtpTbl += tmp_s;
    }

    // combinedKeyFile: sent to the distant recipient, who reconstructs
    // newRandomKeyStr from it via unjoinKeys().
    if (!writeFile(combinedKeyFile, codeGroups(newOtpTbl)))
        return false;

    // NOTE (carried over from otp.py, not fixed here): newRandomKeyStr is
    // half*25 digits (6300 for the default 252-row half), but only the first
    // kPadSize (6250) digits get written out below as the 25 new sheets --
    // the trailing ~50 digits of freshly generated randomness are computed,
    // folded into newOtpTbl above, and then simply never saved by either
    // side. Both joinKeys() and unjoinKeys() apply the same truncation, so
    // the two sides stay in sync; it just means a little of the generated
    // entropy goes unused rather than becoming key material. Flagged in
    // quacque_design.md.
    for (int i = 0; i < kPagesPerPad; ++i) {
        const QString tmpStr = newRandomKeyStr.mid(i * kSheetSize, kSheetSize);
        const QString filename = QStringLiteral("%1-%2.otk").arg(prefix).arg(i + 1, 3, 10, QChar('0'));
        if (!writeFile(filename, codeGroups(tmpStr)))
            return false;
    }

    QString wipeError;
    if (!wipeKeys({fileI, fileJ}))
        wipeError = m_lastError;
    if (!wipeError.isEmpty())
        setError(wipeError);
    else
        clearError();

    return true;
}

bool OTP::unjoinKeys(const QString &fileI, const QString &fileJ,
                      const QString &combinedKeyFile, const QString &prefix)
{
    clearError();
    if (prefix.isEmpty()) {
        setError(QStringLiteral("no key prefix given"));
        return false;
    }

    bool ok = true;
    const QString ki = loadKeyPad(fileI, &ok);
    if (!ok || ki.length() != kSheetSize) {
        setError(QStringLiteral("bad first key (expected %1 digits, got %2)").arg(kSheetSize).arg(ki.length()));
        return false;
    }
    const QString kj = loadKeyPad(fileJ, &ok);
    if (!ok || kj.length() != kSheetSize) {
        setError(QStringLiteral("bad second key (expected %1 digits, got %2)").arg(kSheetSize).arg(kj.length()));
        return false;
    }

    const QVector<QString> iTmp = combinateExpandedKeys(ki, &ok);
    if (!ok) return false;
    const QVector<QString> jTmp = combinateExpandedKeys(kj, &ok);
    if (!ok) return false;

    QVector<QString> tmp = iTmp;
    tmp += jTmp;

    QMap<QString, QString> keys;
    for (const QString &row : tmp) {
        QString checkSum = row.right(5);
        const QString value = row.left(row.length() - 5);

        while (keys.contains(checkSum)) {
            const int csNum = (checkSum.toInt() + 1) % 100000;
            checkSum = QStringLiteral("%1").arg(csNum, 5, 10, QChar('0'));
        }
        keys[checkSum] = value;
    }

    // QMap iterates/values() in ascending key order already, which is
    // exactly Python's explicit "for k in sorted(keys.keys())" walk.
    const QList<QString> sortedValsList = keys.values();
    const QVector<QString> sortedVals(sortedValsList.cbegin(), sortedValsList.cend());
    const int half = sortedVals.size() / 2;
    const QVector<QString> keysA = sortedVals.mid(0, half);
    const QVector<QString> keysB = sortedVals.mid(half);

    QString combinedKeys;
    combinedKeys.reserve(half * 25);
    for (int x = 0; x < half; ++x) {
        combinedKeys += stringAddDigits(keysA[x], keysB[x], &ok);
        if (!ok) return false;
    }

    const QString keyInput = loadKeyPad(combinedKeyFile, &ok);
    if (!ok) return false;
    if (keyInput.length() != combinedKeys.length()) {
        setError(QStringLiteral("combined key file length does not match the expected keypad length"));
        return false;
    }

    // invert joinKeys()'s masking step: ct = K - rd, so rd = K - ct
    const QString newRandomKeyStr = stringSubtractDigits(combinedKeys, keyInput, &ok);
    if (!ok) return false;

    for (int i = 0; i < kPagesPerPad; ++i) {
        const QString tmpStr = newRandomKeyStr.mid(i * kSheetSize, kSheetSize);
        const QString filename = QStringLiteral("%1-%2.otk").arg(prefix).arg(i + 1, 3, 10, QChar('0'));
        if (!writeFile(filename, codeGroups(tmpStr)))
            return false;
    }

    QString wipeError;
    if (!wipeKeys({fileI, fileJ}))
        wipeError = m_lastError;
    if (!wipeError.isEmpty())
        setError(wipeError);
    else
        clearError();

    return true;
}

// ============================================================================
// known-plaintext key recovery ("-f")
// ============================================================================

bool OTP::generateKeyForKnownPlaintext(const QString &plainText, const QString &cipherText,
                                        const QString &keyOutputFile)
{
    clearError();
    bool ok = true;

    const QString msgP = encode(plainText, &ok);
    if (!ok)
        return false;

    const QString msgC = stringDigits(fromMorseCut(cipherText));

    if (msgC.length() != msgP.length()) {
        setError(QStringLiteral("ciphertext and encoded plaintext must be the same length "
                                 "(ciphertext: %1 digits, plaintext: %2 digits)")
                     .arg(msgC.length()).arg(msgP.length()));
        return false;
    }

    const QString kStr = stringSubtractDigits(msgP, msgC, &ok);
    if (!ok)
        return false;

    return writeFile(keyOutputFile, codeGroups(kStr));
}

// ============================================================================
// general m-of-n combinations, message split / merge
// ============================================================================

QVector<QString> OTP::combo(int m, int n)
{
    // Standard lexicographic "next combination" walk over {1..n} choose m --
    // produces the same order as Python's itertools.combinations(range(1,
    // n+1), m), which otp.py's combo() wraps.
    QVector<QString> result;
    if (m <= 0 || m > n)
        return result;

    QVector<int> idx(m);
    for (int i = 0; i < m; ++i)
        idx[i] = i; // 0-based; courier number is idx[i] + 1

    while (true) {
        QString s;
        for (int i = 0; i < m; ++i)
            s += QString::number(idx[i] + 1);
        result.append(s);

        int i = m - 1;
        while (i >= 0 && idx[i] == n - m + i)
            --i;
        if (i < 0)
            break; // that was the last combination
        ++idx[i];
        for (int j = i + 1; j < m; ++j)
            idx[j] = idx[j - 1] + 1;
    }
    return result;
}

QVector<QString> OTP::splitIntoShares(const QString &msgDigits, int shareCount)
{
    // otp.py's doMsgSplit(): shareCount-1 fresh random strings, plus the
    // message minus their sum -- so all shareCount shares are needed to
    // recover msgDigits (any shareCount-1 of them reveal nothing about it).
    QVector<QString> tbl;
    tbl.reserve(shareCount);

    QString tmp = msgDigits;
    for (int i = 0; i < shareCount - 1; ++i) {
        const QString r = randDigits(msgDigits.length());
        tbl.append(r);
        bool ok = true;
        tmp = stringSubtractDigits(tmp, r, &ok);
        // r and tmp are always the same length here (both msgDigits.length()),
        // so stringSubtractDigits() can't actually fail on the length check.
    }
    tbl.append(tmp);
    return tbl;
}

bool OTP::splitMessage(const QString &plainText, int minParts, int maxParts, const QString &filePrefix)
{
    clearError();

    if (minParts < 1 || maxParts < minParts) {
        setError(QStringLiteral("minParts/maxParts must satisfy 1 <= minParts <= maxParts"));
        return false;
    }
    if (maxParts > 9) {
        // otp.py's do_splitMsg() names each courier by indexing into the
        // combination's concatenated-digit-string ID by character position
        // ("messageGroup[i]") -- that silently breaks once any courier
        // number reaches two digits (courier 10+), misaligning filenames
        // instead of erroring. Refuse it outright rather than reproduce
        // that silent misbehavior -- see quacque_design.md.
        setError(QStringLiteral("maxParts > 9 is not supported (courier numbering "
                                 "becomes ambiguous past single digits)"));
        return false;
    }
    if (plainText.isEmpty()) {
        setError(QStringLiteral("message is empty, nothing to split"));
        return false;
    }

    bool ok = true;
    const QString encoded = encode(plainText, &ok);
    if (!ok)
        return false;
    const QString clearTextDigits = stringDigits(encoded);

    // otp.py wipes the source plaintext *file* here; splitMessage() takes
    // the plaintext as in-memory text (no file of its own to wipe) -- the
    // caller that read it from a file (OtpCli.cpp for the console tool) is
    // responsible for wiping that source file itself after a successful
    // split, via wipeFiles().
    for (const QString &messageGroup : combo(minParts, maxParts)) {
        const QVector<QString> shares = splitIntoShares(clearTextDigits, minParts);

        for (int i = 0; i < messageGroup.length(); ++i) {
            const QString fileName = filePrefix + messageGroup[i] + QLatin1Char('-')
                                      + messageGroup + QStringLiteral(".otp");
            const QString fileData = codeGroups(shares[i]) + QStringLiteral("\n");
            if (!writeFile(fileName, fileData))
                return false;
        }
    }

    clearError();
    return true;
}

QString OTP::mergeMessage(const QStringList &segmentFiles)
{
    clearError();

    if (segmentFiles.isEmpty()) {
        setError(QStringLiteral("no segment files given"));
        return QString();
    }

    bool ok = true;
    const QString firstText = readFile(segmentFiles.first(), &ok);
    if (!ok)
        return QString();

    const int fileLen = stringDigits(firstText).length();
    if (fileLen < 1) {
        setError(QStringLiteral("segment file %1 contains no text").arg(segmentFiles.first()));
        return QString();
    }

    // matches otp.py's do_mergeMsg() exactly, including reading
    // segmentFiles.first() again inside this loop -- redundant, but this is
    // a faithful port, not a rewrite.
    QString clearTextDigits(fileLen, QChar('0'));
    for (const QString &filename : segmentFiles) {
        const QString raw = readFile(filename, &ok);
        if (!ok)
            return QString();
        const QString tmpText = stringDigits(raw);

        if (tmpText.length() != clearTextDigits.length()) {
            setError(QStringLiteral("message length of file %1 does not match").arg(filename));
            return QString();
        }
        clearTextDigits = stringAddDigits(clearTextDigits, tmpText, &ok);
        if (!ok)
            return QString();
    }

    const QString plainText = decode(clearTextDigits, &ok);
    if (!ok)
        return QString();

    QString wipeError;
    if (!wipeKeys(segmentFiles))
        wipeError = m_lastError;
    if (!wipeError.isEmpty())
        setError(wipeError);
    else
        clearError();

    return plainText;
}

// ============================================================================
// stream combine ("-b")
// ============================================================================

bool OTP::combineStreams(const QString &inputFile1, const QString &inputFile2,
                          const QString &combinedFile, const QString &keyPrefix)
{
    clearError();
    bool ok = true;

    const QString tmp1 = stringDigits(readFile(inputFile1, &ok));
    if (!ok)
        return false;
    const QString tmp2 = stringDigits(readFile(inputFile2, &ok));
    if (!ok)
        return false;

    if (tmp1.length() != tmp2.length()) {
        setError(QStringLiteral("code stream digit counts are different"));
        return false;
    }

    const QString tmp3 = stringAddDigits(tmp1, tmp2, &ok);
    if (!ok)
        return false;

    if (!combinedFile.isEmpty()) {
        if (!writeFile(combinedFile, codeGroups(tmp3)))
            return false;
    }

    if (!keyPrefix.isEmpty()) {
        // 0-based page numbering here, unlike keygen()'s 1-based -- matches
        // otp.py's do_combineStreams() exactly.
        const int digitsNeeded = tmp3.length();
        const int pageCount = digitsNeeded / kSheetSize;
        for (int x = 0; x < pageCount; ++x) {
            const QString fn = QStringLiteral("%1-%2.otk").arg(keyPrefix).arg(x, 2, 10, QChar('0'));
            const QString s = tmp3.mid(x * kSheetSize, kSheetSize);
            if (!writeFile(fn, codeGroups(s)))
                return false;
        }
    }

    QString wipeError;
    if (!wipeKeys({inputFile1, inputFile2}))
        wipeError = m_lastError;
    if (!wipeError.isEmpty())
        setError(wipeError);
    else
        clearError();

    return true;
}

// ============================================================================
// wipe ("-w")
// ============================================================================

bool OTP::wipeFiles(const QStringList &files)
{
    clearError();
    bool allOk = true;
    for (const QString &fn : files) {
        if (!wipeFile(fn))
            allOk = false;
    }
    if (!allOk && m_lastError.isEmpty())
        setError(QStringLiteral("one or more files failed to wipe"));
    return allOk;
}
