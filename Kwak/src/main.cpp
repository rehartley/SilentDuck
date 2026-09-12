// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#include "OTP.h"
#include "OtpCli.h"
#include "SecureRandom.h"
#include "Utf8.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// main.cpp -- Kwak's console entry point, built as "otp" (otp.exe on
// Windows). Two modes:
//
//   otp             runs otp_main() (OtpCli.cpp): a real argv-driven CLI,
//                     functionally equivalent to otp.py/Quacque, built on
//                     OTP's public API.
//   otp --selftest  runs runSmokeTests() below instead: the regression
//                     check exercised throughout OTP.cpp's development
//                     (round trips for every operation, entirely inside a
//                     throwaway temp directory so it leaves nothing
//                     behind). Kept as a fast, no-file-arguments sanity
//                     check independent of the CLI parsing layer.

namespace {

// RAII temp directory -- replaces Quacque's QTemporaryDir. Creates a
// uniquely-named directory under the system temp path and recursively
// removes it (and everything the smoke test wrote into it) on destruction.
struct TempDir
{
    std::filesystem::path path;
    bool valid = false;

    TempDir()
    {
        unsigned char suffix[8];
        secure_random::fillBytes(suffix, sizeof(suffix));
        static const char hex[] = "0123456789abcdef";
        std::string name = "otp_selftest_";
        for (unsigned char b : suffix) {
            name += hex[b >> 4];
            name += hex[b & 0x0F];
        }

        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec) / name;
        if (ec)
            return;
        valid = std::filesystem::create_directory(path, ec) && !ec;
    }

    ~TempDir()
    {
        if (valid) {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    }

    std::string file(const std::string &name) const { return (path / name).string(); }
};

bool copyFileOverwrite(const std::string &from, const std::string &to)
{
    std::error_code ec;
    std::filesystem::remove(to, ec);
    return std::filesystem::copy_file(from, to, ec) && !ec;
}

std::string stripFormatting(const std::string &s)
{
    std::string tmp;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9')
            tmp += ch;
    }
    return tmp;
}

std::string readFileRaw(const std::string &fn)
{
    std::ifstream f(fn, std::ios::binary);
    if (!f)
        return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string u32ToUtf8(const std::u32string &s) { return utf8::encode(s); }
std::u32string u(const char *s) { return utf8::decode(s); }

int runSmokeTests()
{
    TempDir dir;
    if (!dir.valid) {
        std::cerr << "[FAIL] could not create temp dir for smoke test\n";
        return 1;
    }
    int failures = 0;

    // ---- 1. keygen -> encipher -> decipher ----
    {
        OTP keyGenOtp;
        const std::string prefix = dir.file("msgkey");
        if (!keyGenOtp.keygen(prefix, /*zeroKeys=*/false, /*pagesPerPad=*/1)) {
            std::cerr << "[FAIL] keygen failed: " << keyGenOtp.lastError() << "\n";
            ++failures;
        } else {
            const std::string keySheet = prefix + "-001.otk";
            const std::string senderCopy = dir.file("sender.otk");
            const std::string receiverCopy = dir.file("receiver.otk");
            // encipher()/decipher() each wipe their own key file after use,
            // so sender and receiver need separate copies of the same key
            // content, exactly as they would in real use.
            copyFileOverwrite(keySheet, senderCopy);
            copyFileOverwrite(keySheet, receiverCopy);
            std::filesystem::remove(keySheet);

            const std::u32string plain = u("HELLO FROM KWAK");

            OTP encOtp;
            const std::optional<std::u32string> cipher = encOtp.encipher(plain, {senderCopy});
            if (!cipher) {
                std::cerr << "[FAIL] encipher failed: " << encOtp.lastError() << "\n";
                ++failures;
            } else {
                OTP decOtp;
                const std::optional<std::u32string> roundTrip = decOtp.decipher(*cipher, {receiverCopy});
                if (!roundTrip) {
                    std::cerr << "[FAIL] decipher failed: " << decOtp.lastError() << "\n";
                    ++failures;
                } else if (*roundTrip != plain) {
                    std::cerr << "[FAIL] round trip mismatch: sent " << u32ToUtf8(plain) << " got " << u32ToUtf8(*roundTrip) << "\n";
                    ++failures;
                } else {
                    std::cout << "[PASS] keygen -> encipher -> decipher round trip: " << u32ToUtf8(*roundTrip) << "\n";
                }
            }
        }
    }

    // ---- 2. keygen x2 -> joinKeys / unjoinKeys ----
    {
        OTP keyGenOtp;
        const std::string prefixA = dir.file("sheetA");
        const std::string prefixB = dir.file("sheetB");
        const bool ok = keyGenOtp.keygen(prefixA, false, 1) && keyGenOtp.keygen(prefixB, false, 1);
        if (!ok) {
            std::cerr << "[FAIL] keygen for join/unjoin test failed: " << keyGenOtp.lastError() << "\n";
            ++failures;
        } else {
            const std::string sheetA = prefixA + "-001.otk";
            const std::string sheetB = prefixB + "-001.otk";

            // join and unjoin each wipe fileI/fileJ after use, so each side
            // needs its own copy of the same two starting sheets.
            const std::string joinA = dir.file("joinA.otk");
            const std::string joinB = dir.file("joinB.otk");
            const std::string unjoinA = dir.file("unjoinA.otk");
            const std::string unjoinB = dir.file("unjoinB.otk");
            copyFileOverwrite(sheetA, joinA);
            copyFileOverwrite(sheetB, joinB);
            copyFileOverwrite(sheetA, unjoinA);
            copyFileOverwrite(sheetB, unjoinB);

            const std::string combinedFile = dir.file("combined.otk");
            const std::string joinPrefix = dir.file("join_out");
            const std::string unjoinPrefix = dir.file("unjoin_out");

            OTP joinOtp;
            if (!joinOtp.joinKeys(joinA, joinB, combinedFile, joinPrefix)) {
                std::cerr << "[FAIL] joinKeys failed: " << joinOtp.lastError() << "\n";
                ++failures;
            } else {
                OTP unjoinOtp;
                if (!unjoinOtp.unjoinKeys(unjoinA, unjoinB, combinedFile, unjoinPrefix)) {
                    std::cerr << "[FAIL] unjoinKeys failed: " << unjoinOtp.lastError() << "\n";
                    ++failures;
                } else {
                    bool allMatch = true;
                    for (int i = 1; i <= 25; ++i) {
                        char buf[4];
                        std::snprintf(buf, sizeof(buf), "%03d", i);
                        // joinKeys()/unjoinKeys() both write "<prefix>-NNN.otk"
                        // (3-digit, dashed) -- this used to differ between the
                        // two (see the "Fixed" entry in kwak_design.md/
                        // quacque_design.md's otp.py-inconsistencies section),
                        // and this test's old 2-digit/no-dash-vs-dash filenames
                        // were never updated to match once that asymmetry was
                        // fixed. Caught by actually running this smoke test
                        // against real files instead of trusting it unchanged.
                        const std::string joinSheet = joinPrefix + "-" + buf + ".otk";
                        const std::string unjoinSheet = unjoinPrefix + "-" + buf + ".otk";

                        const std::string jContent = stripFormatting(readFileRaw(joinSheet));
                        const std::string uContent = stripFormatting(readFileRaw(unjoinSheet));

                        if (jContent.empty() || jContent != uContent) {
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
        const std::u32string plain = u("MEET AT DAWN");
        const std::string prefix = dir.file("splitA-");

        if (!otp.splitMessage(plain, /*minParts=*/2, /*maxParts=*/4, prefix)) {
            std::cerr << "[FAIL] splitMessage failed: " << otp.lastError() << "\n";
            ++failures;
        } else {
            // combo(2, 4)'s first group is "12" -- couriers 1 and 2 each
            // hold one share of that group; any 2 of a group's shares
            // should merge back to the original message.
            const std::vector<std::string> shareFiles = {
                prefix + "1-12.otp",
                prefix + "2-12.otp",
            };
            const std::optional<std::u32string> merged = otp.mergeMessage(shareFiles);
            if (!merged) {
                std::cerr << "[FAIL] mergeMessage failed: " << otp.lastError() << "\n";
                ++failures;
            } else if (*merged != plain) {
                std::cerr << "[FAIL] split/merge round trip mismatch: sent " << u32ToUtf8(plain) << " got " << u32ToUtf8(*merged) << "\n";
                ++failures;
            } else {
                std::cout << "[PASS] splitMessage -> mergeMessage (2-of-4 courier shares) round trip: " << u32ToUtf8(*merged) << "\n";
            }
        }
    }

    // ---- 4. generateKeyForKnownPlaintext -- repudiation property ----
    // A real key enciphers a real message; a manufactured key should make
    // that SAME ciphertext decipher to a different, chosen plaintext.
    {
        OTP keyGenOtp;
        const std::string prefix = dir.file("fakemsgkey");
        keyGenOtp.keygen(prefix, false, 1);
        const std::string realKey = prefix + "-001.otk";
        const std::string realKeyCopy = dir.file("fakemsgkey_copy.otk");
        copyFileOverwrite(realKey, realKeyCopy);
        std::filesystem::remove(realKey);

        const std::u32string realPlain = u("ATTACK AT DAWN");
        OTP encOtp;
        const std::optional<std::u32string> cipher = encOtp.encipher(realPlain, {realKeyCopy});

        if (!cipher) {
            std::cerr << "[FAIL] fakeMsg setup: encipher failed: " << encOtp.lastError() << "\n";
            ++failures;
        } else {
            // Cover plaintext must encode to the exact same digit length as
            // realPlain -- reversing the string guarantees an identical
            // character multiset (so an identical encoded length) without
            // having to hand-count checkerboard digits per letter.
            std::u32string coverPlain = realPlain;
            std::reverse(coverPlain.begin(), coverPlain.end());
            OTP fakeOtp;
            const std::string fakeKeyFile = dir.file("fake.otk");
            const bool genOk = fakeOtp.generateKeyForKnownPlaintext(coverPlain, *cipher, fakeKeyFile);
            if (!genOk) {
                std::cerr << "[FAIL] generateKeyForKnownPlaintext failed: " << fakeOtp.lastError() << "\n";
                ++failures;
            } else {
                OTP checkOtp;
                const std::optional<std::u32string> recovered = checkOtp.decipher(*cipher, {fakeKeyFile});
                if (!recovered || *recovered != coverPlain) {
                    std::cerr << "[FAIL] fake key did not reproduce the cover plaintext: got "
                              << (recovered ? u32ToUtf8(*recovered) : std::string("<decipher failed>")) << "\n";
                    ++failures;
                } else {
                    std::cout << "[PASS] generateKeyForKnownPlaintext: same ciphertext deciphers to the chosen cover text: "
                              << u32ToUtf8(*recovered) << "\n";
                }
            }
        }
    }

    // ---- 5. combineStreams + wipeFiles ----
    {
        OTP keyGenOtp;
        keyGenOtp.keygen(dir.file("streamA"), false, 1);
        keyGenOtp.keygen(dir.file("streamB"), false, 1);
        const std::string streamA = dir.file("streamA-001.otk");
        const std::string streamB = dir.file("streamB-001.otk");
        const std::string combined = dir.file("combinedStream.otk");

        OTP combineOtp;
        if (!combineOtp.combineStreams(streamA, streamB, combined, std::string())) {
            std::cerr << "[FAIL] combineStreams failed: " << combineOtp.lastError() << "\n";
            ++failures;
        } else {
            const std::string combinedDigits = stripFormatting(readFileRaw(combined));
            if (static_cast<int>(combinedDigits.length()) != OTP::kSheetSize) {
                std::cerr << "[FAIL] combineStreams: expected " << OTP::kSheetSize
                          << " digits, got " << combinedDigits.length() << "\n";
                ++failures;
            } else {
                std::cout << "[PASS] combineStreams produced a " << combinedDigits.length() << "-digit combined stream\n";
            }

            OTP wipeOtp;
            if (!wipeOtp.wipeFiles({combined}) || std::filesystem::exists(combined)) {
                std::cerr << "[FAIL] wipeFiles did not remove: " << combined << "\n";
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
#ifdef _WIN32
    // Windows consoles commonly default to a legacy single-byte codepage
    // (437, 1252, ...) rather than UTF-8. Every string this app writes to
    // stdout/stderr (message content, error text, and -hh's Cyrillic
    // reference tables) is UTF-8 bytes -- under a legacy codepage those get
    // garbled into mojibake (each 2-byte Cyrillic sequence splits into two
    // wrong single-byte glyphs). otp.py hits the identical problem and
    // works around it by reconfiguring its own stdout/stderr encoding at
    // startup (see the comment at the top of otp.py); SetConsoleOutputCP()
    // is the OS-level equivalent here -- the same thing "chcp 65001" does
    // interactively, done automatically so a fresh console never needs
    // that run by hand first. Safe to call even with no console attached
    // (output fully redirected to a file/pipe): it just fails harmlessly,
    // which is fine to ignore since nothing here depends on it succeeding.
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc > 1 && std::string(argv[1]) == "--selftest")
        return runSmokeTests();

    return otp_main(argc, argv);
}
