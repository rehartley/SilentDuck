#include "OTP.h"
#include "OtpCli.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>

// main.cpp -- Quacque's console entry point. Two modes:
//
//   quacque_console            runs otp_main() (OtpCli.cpp): a real
//                                argv-driven CLI, functionally equivalent
//                                to otp.py, built on OTP's public API.
//   quacque_console --selftest runs runSmokeTests() below instead: the
//                                regression check exercised throughout
//                                OTP.cpp's development (round trips for
//                                every operation, entirely inside a
//                                QTemporaryDir so it leaves nothing
//                                behind). Kept as a fast, no-file-arguments
//                                sanity check independent of the CLI
//                                parsing layer -- delete once real
//                                automated tests take over.
//
// No QCoreApplication/QApplication is constructed: TerminalEditor.cpp's
// "EDITOR" implementation is curses-based, not Qt GUI, so there's no event
// loop to own here -- QString/QFile/QTemporaryDir etc. all work fine
// without one.

namespace {

bool copyFile(const QString &from, const QString &to)
{
    QFile::remove(to);
    return QFile::copy(from, to);
}

QString stripFormatting(const QString &s)
{
    QString tmp;
    for (const QChar &ch : s) {
        if (ch.isDigit())
            tmp += ch;
    }
    return tmp;
}

QString readFileRaw(const QString &fn)
{
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// std::cout/cerr have no operator<< for QString (that's QDebug-specific);
// this build uses plain iostreams throughout rather than qInfo()/qCritical()
// because Qt's default message handler was producing no visible output at
// all on this MinGW/Windows console build -- a real diagnostic gap worth
// knowing about for any future Qt console tool on this toolchain.
std::string qs(const QString &s) { return s.toStdString(); }

int runSmokeTests()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::cerr << "[FAIL] could not create temp dir for smoke test\n";
        return 1;
    }
    const QString base = dir.path() + QLatin1Char('/');
    int failures = 0;

    // ---- 1. keygen -> encipher -> decipher ----
    {
        OTP keyGenOtp;
        const QString prefix = base + QStringLiteral("msgkey");
        if (!keyGenOtp.keygen(prefix, /*zeroKeys=*/false, /*pagesPerPad=*/1)) {
            std::cerr << "[FAIL] keygen failed: " << qs(keyGenOtp.lastError()) << "\n";
            ++failures;
        } else {
            const QString keySheet = prefix + QStringLiteral("-001.otk");
            const QString senderCopy = base + QStringLiteral("sender.otk");
            const QString receiverCopy = base + QStringLiteral("receiver.otk");
            // encipher()/decipher() each wipe their own key file after use,
            // so sender and receiver need separate copies of the same key
            // content, exactly as they would in real use.
            copyFile(keySheet, senderCopy);
            copyFile(keySheet, receiverCopy);
            QFile::remove(keySheet);

            const QString plain = QStringLiteral("HELLO FROM QUACQUE");

            OTP encOtp;
            const QString cipher = encOtp.encipher(plain, {senderCopy});
            if (cipher.isNull()) {
                std::cerr << "[FAIL] encipher failed: " << qs(encOtp.lastError()) << "\n";
                ++failures;
            } else {
                OTP decOtp;
                const QString roundTrip = decOtp.decipher(cipher, {receiverCopy});
                if (roundTrip.isNull()) {
                    std::cerr << "[FAIL] decipher failed: " << qs(decOtp.lastError()) << "\n";
                    ++failures;
                } else if (roundTrip != plain) {
                    std::cerr << "[FAIL] round trip mismatch: sent " << qs(plain) << " got " << qs(roundTrip) << "\n";
                    ++failures;
                } else {
                    std::cout << "[PASS] keygen -> encipher -> decipher round trip: " << qs(roundTrip) << "\n";
                }
            }
        }
    }

    // ---- 2. keygen x2 -> joinKeys / unjoinKeys ----
    {
        OTP keyGenOtp;
        const QString prefixA = base + QStringLiteral("sheetA");
        const QString prefixB = base + QStringLiteral("sheetB");
        const bool ok = keyGenOtp.keygen(prefixA, false, 1) && keyGenOtp.keygen(prefixB, false, 1);
        if (!ok) {
            std::cerr << "[FAIL] keygen for join/unjoin test failed: " << qs(keyGenOtp.lastError()) << "\n";
            ++failures;
        } else {
            const QString sheetA = prefixA + QStringLiteral("-001.otk");
            const QString sheetB = prefixB + QStringLiteral("-001.otk");

            // join and unjoin each wipe fileI/fileJ after use, so each side
            // needs its own copy of the same two starting sheets.
            const QString joinA = base + QStringLiteral("joinA.otk");
            const QString joinB = base + QStringLiteral("joinB.otk");
            const QString unjoinA = base + QStringLiteral("unjoinA.otk");
            const QString unjoinB = base + QStringLiteral("unjoinB.otk");
            copyFile(sheetA, joinA);
            copyFile(sheetB, joinB);
            copyFile(sheetA, unjoinA);
            copyFile(sheetB, unjoinB);

            const QString combinedFile = base + QStringLiteral("combined.otk");
            const QString joinPrefix = base + QStringLiteral("join_out");
            const QString unjoinPrefix = base + QStringLiteral("unjoin_out");

            OTP joinOtp;
            if (!joinOtp.joinKeys(joinA, joinB, combinedFile, joinPrefix)) {
                std::cerr << "[FAIL] joinKeys failed: " << qs(joinOtp.lastError()) << "\n";
                ++failures;
            } else {
                OTP unjoinOtp;
                if (!unjoinOtp.unjoinKeys(unjoinA, unjoinB, combinedFile, unjoinPrefix)) {
                    std::cerr << "[FAIL] unjoinKeys failed: " << qs(unjoinOtp.lastError()) << "\n";
                    ++failures;
                } else {
                    bool allMatch = true;
                    for (int i = 1; i <= 25; ++i) {
                        // NOTE: join and unjoin name their output sheets
                        // differently ("prefixNN.otk" vs "prefix-NN.otk") --
                        // see OTP.h's doc comment on joinKeys()/unjoinKeys().
                        const QString joinSheet =
                            QStringLiteral("%1%2.otk").arg(joinPrefix).arg(i, 2, 10, QChar('0'));
                        const QString unjoinSheet =
                            QStringLiteral("%1-%2.otk").arg(unjoinPrefix).arg(i, 2, 10, QChar('0'));

                        const QString jContent = stripFormatting(readFileRaw(joinSheet));
                        const QString uContent = stripFormatting(readFileRaw(unjoinSheet));

                        if (jContent.isEmpty() || jContent != uContent) {
                            std::cerr << "[FAIL] sheet " << i << " mismatch between join and unjoin output\n";
                            allMatch = false;
                        }
                    }
                    if (allMatch) {
                        std::cout << "[PASS] joinKeys / unjoinKeys derived an identical 25-sheet keypad\n";
                    } else {
                        ++failures;
                    }
                }
            }
        }
    }

    // ---- 3. splitMessage -> mergeMessage (2-of-4 courier shares) ----
    {
        OTP otp;
        const QString plain = QStringLiteral("MEET AT DAWN");
        const QString prefix = base + QStringLiteral("splitA-");

        if (!otp.splitMessage(plain, /*minParts=*/2, /*maxParts=*/4, prefix)) {
            std::cerr << "[FAIL] splitMessage failed: " << qs(otp.lastError()) << "\n";
            ++failures;
        } else {
            // combo(2, 4)'s first group is "12" -- couriers 1 and 2 each
            // hold one share of that group; any 2 of a group's shares
            // should merge back to the original message.
            const QStringList shareFiles = {
                prefix + QStringLiteral("1-12.otp"),
                prefix + QStringLiteral("2-12.otp"),
            };
            const QString merged = otp.mergeMessage(shareFiles);
            if (merged.isNull()) {
                std::cerr << "[FAIL] mergeMessage failed: " << qs(otp.lastError()) << "\n";
                ++failures;
            } else if (merged != plain) {
                std::cerr << "[FAIL] split/merge round trip mismatch: sent " << qs(plain) << " got " << qs(merged) << "\n";
                ++failures;
            } else {
                std::cout << "[PASS] splitMessage -> mergeMessage (2-of-4 courier shares) round trip: " << qs(merged) << "\n";
            }
        }
    }

    // ---- 4. generateKeyForKnownPlaintext -- repudiation property ----
    // A real key encipher a real message; a manufactured key should make
    // that SAME ciphertext decipher to a different, chosen plaintext.
    {
        OTP keyGenOtp;
        const QString prefix = base + QStringLiteral("fakemsgkey");
        keyGenOtp.keygen(prefix, false, 1);
        const QString realKey = prefix + QStringLiteral("-001.otk");
        const QString realKeyCopy = base + QStringLiteral("fakemsgkey_copy.otk");
        copyFile(realKey, realKeyCopy);
        QFile::remove(realKey);

        const QString realPlain = QStringLiteral("ATTACK AT DAWN");
        OTP encOtp;
        const QString cipher = encOtp.encipher(realPlain, {realKeyCopy});

        if (cipher.isNull()) {
            std::cerr << "[FAIL] fakeMsg setup: encipher failed: " << qs(encOtp.lastError()) << "\n";
            ++failures;
        } else {
            // Cover plaintext must encode to the exact same digit length as
            // realPlain -- reversing the string guarantees an identical
            // character multiset (so an identical encoded length) without
            // having to hand-count checkerboard digits per letter.
            QString coverPlain = realPlain;
            std::reverse(coverPlain.begin(), coverPlain.end());
            OTP fakeOtp;
            const QString fakeKeyFile = base + QStringLiteral("fake.otk");
            const bool genOk = fakeOtp.generateKeyForKnownPlaintext(coverPlain, cipher, fakeKeyFile);
            if (!genOk) {
                std::cerr << "[FAIL] generateKeyForKnownPlaintext failed: " << qs(fakeOtp.lastError()) << "\n";
                ++failures;
            } else {
                OTP checkOtp;
                const QString recovered = checkOtp.decipher(cipher, {fakeKeyFile});
                if (recovered != coverPlain) {
                    std::cerr << "[FAIL] fake key did not reproduce the cover plaintext: got " << qs(recovered) << "\n";
                    ++failures;
                } else {
                    std::cout << "[PASS] generateKeyForKnownPlaintext: same ciphertext deciphers to the chosen cover text: " << qs(recovered) << "\n";
                }
            }
        }
    }

    // ---- 5. combineStreams + wipeFiles ----
    {
        OTP keyGenOtp;
        keyGenOtp.keygen(base + QStringLiteral("streamA"), false, 1);
        keyGenOtp.keygen(base + QStringLiteral("streamB"), false, 1);
        const QString streamA = base + QStringLiteral("streamA-001.otk");
        const QString streamB = base + QStringLiteral("streamB-001.otk");
        const QString combined = base + QStringLiteral("combinedStream.otk");

        OTP combineOtp;
        if (!combineOtp.combineStreams(streamA, streamB, combined, QString())) {
            std::cerr << "[FAIL] combineStreams failed: " << qs(combineOtp.lastError()) << "\n";
            ++failures;
        } else {
            const QString combinedDigits = stripFormatting(readFileRaw(combined));
            if (combinedDigits.length() != OTP::kSheetSize) {
                std::cerr << "[FAIL] combineStreams: expected " << OTP::kSheetSize
                          << " digits, got " << combinedDigits.length() << "\n";
                ++failures;
            } else {
                std::cout << "[PASS] combineStreams produced a " << combinedDigits.length() << "-digit combined stream\n";
            }

            OTP wipeOtp;
            if (!wipeOtp.wipeFiles({combined}) || QFile::exists(combined)) {
                std::cerr << "[FAIL] wipeFiles did not remove: " << qs(combined) << "\n";
                ++failures;
            } else {
                std::cout << "[PASS] wipeFiles removed the combined stream file\n";
            }
        }
    }

    if (failures == 0) {
        std::cout << "[INFO] ALL SMOKE TESTS PASSED\n";
        return 0;
    }
    std::cerr << "[FAIL] " << failures << " smoke test(s) FAILED\n";
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--selftest"))
        return runSmokeTests();

    return otp_main(argc, argv);
}
