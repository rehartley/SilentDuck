// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Optional;

// Main -- Kwek's console entry point, run as "java -cp build Main" (or a
// packaged "kwek.jar" once one exists -- see kwek_design.md's open
// questions). Two modes, same split as Kwak's main.cpp:
//
//   (no args, or any flag)   runs OtpCli.run() -- a real argv-driven CLI,
//                             functionally equivalent to otp.py/Quacque/
//                             Kwak, built on OTP's public API.
//   --selftest                runs runSmokeTests() below instead: the same
//                             five regression checks Kwak's own --selftest
//                             runs (see main.cpp), entirely inside a
//                             throwaway temp directory so it leaves nothing
//                             behind. Kept as a fast, no-file-arguments
//                             sanity check independent of the CLI parsing
//                             layer.
final class Main {
    private Main() { }

    public static void main(String[] args) {
        // Force UTF-8 stdout/stderr regardless of the JVM's own default
        // charset detection. This is the write-side half of the same
        // problem Kwak's main.cpp solves with SetConsoleOutputCP(CP_UTF8):
        // every string this app prints (message content, error text, and
        // -hh's Cyrillic reference tables) is meant to be UTF-8.
        //
        // What this does NOT fully replicate: SetConsoleOutputCP() also
        // changes the *Windows console's own* codepage, so a legacy-codepage
        // console correctly displays the bytes Kwak sends it. Kwek has no
        // portable way to call that same WinAPI function without a native
        // binding (which would break the "just javac" build), so a fresh
        // Windows console may still need `chcp 65001` run once (or Windows'
        // "Use Unicode UTF-8 for worldwide language support" setting) before
        // Cyrillic text in -hh's reference tables or a deciphered message
        // renders correctly here -- flagged honestly, not glossed over. See
        // kwek_design.md.
        System.setOut(new PrintStream(new FileOutputStream(FileDescriptor.out), true, StandardCharsets.UTF_8));
        System.setErr(new PrintStream(new FileOutputStream(FileDescriptor.err), true, StandardCharsets.UTF_8));

        if (args.length > 0 && args[0].equals("--selftest")) {
            System.exit(runSmokeTests());
            return;
        }

        System.exit(OtpCli.run(args));
    }

    // ========================================================================
    // --selftest support
    // ========================================================================

    // RAII-style temp directory, Java-fashion: created in the constructor,
    // recursively removed by close() -- used with try-with-resources below.
    // Replaces Kwak's hand-rolled TempDir (which builds its own unique name
    // from a random hex suffix): Files.createTempDirectory() already gives a
    // securely unique directory under the system temp path in one call.
    private static final class TempDir implements AutoCloseable {
        final Path path;

        TempDir() throws IOException {
            path = Files.createTempDirectory("otp_selftest_");
        }

        String file(String name) {
            return path.resolve(name).toString();
        }

        @Override
        public void close() {
            try (var walk = Files.walk(path)) {
                walk.sorted(Comparator.reverseOrder()).forEach(p -> {
                    try { Files.deleteIfExists(p); } catch (IOException ignored) { }
                });
            } catch (IOException ignored) {
                // best-effort cleanup, same as Kwak's TempDir destructor
            }
        }
    }

    private static boolean copyFileOverwrite(String from, String to) {
        try {
            Files.copy(Paths.get(from), Paths.get(to), StandardCopyOption.REPLACE_EXISTING);
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    private static String stripFormatting(String s) {
        StringBuilder tmp = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch >= '0' && ch <= '9')
                tmp.append(ch);
        }
        return tmp.toString();
    }

    private static String readFileRaw(String fn) {
        try {
            return new String(Files.readAllBytes(Paths.get(fn)), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    private static int runSmokeTests() {
        TempDir dir;
        try {
            dir = new TempDir();
        } catch (IOException e) {
            System.err.println("[FAIL] could not create temp dir for smoke test");
            return 1;
        }

        int failures = 0;
        try {
            // ---- 1. keygen -> encipher -> decipher ----
            {
                OTP keyGenOtp = new OTP();
                String prefix = dir.file("msgkey");
                if (!keyGenOtp.keygen(prefix, /*zeroKeys=*/false, /*pagesPerPad=*/1, /*trigon=*/false)) {
                    System.err.println("[FAIL] keygen failed: " + keyGenOtp.lastError());
                    failures++;
                } else {
                    String keySheet = prefix + "-001.otk";
                    String senderCopy = dir.file("sender.otk");
                    String receiverCopy = dir.file("receiver.otk");
                    // encipher()/decipher() each wipe their own key file
                    // after use, so sender and receiver need separate copies
                    // of the same key content, exactly as they would in real
                    // use.
                    copyFileOverwrite(keySheet, senderCopy);
                    copyFileOverwrite(keySheet, receiverCopy);
                    try { Files.deleteIfExists(Paths.get(keySheet)); } catch (IOException ignored) { }

                    String plain = "HELLO FROM KWEK";

                    OTP encOtp = new OTP();
                    Optional<String> cipher = encOtp.encipher(plain, List.of(senderCopy));
                    if (cipher.isEmpty()) {
                        System.err.println("[FAIL] encipher failed: " + encOtp.lastError());
                        failures++;
                    } else {
                        OTP decOtp = new OTP();
                        Optional<String> roundTrip = decOtp.decipher(cipher.get(), List.of(receiverCopy));
                        if (roundTrip.isEmpty()) {
                            System.err.println("[FAIL] decipher failed: " + decOtp.lastError());
                            failures++;
                        } else if (!roundTrip.get().equals(plain)) {
                            System.err.println("[FAIL] round trip mismatch: sent " + plain + " got " + roundTrip.get());
                            failures++;
                        } else {
                            System.out.println("[PASS] keygen -> encipher -> decipher round trip: " + roundTrip.get());
                        }
                    }
                }
            }

            // ---- 2. keygen x2 -> joinKeys / unjoinKeys ----
            {
                OTP keyGenOtp = new OTP();
                String prefixA = dir.file("sheetA");
                String prefixB = dir.file("sheetB");
                boolean ok = keyGenOtp.keygen(prefixA, false, 1, false) && keyGenOtp.keygen(prefixB, false, 1, false);
                if (!ok) {
                    System.err.println("[FAIL] keygen for join/unjoin test failed: " + keyGenOtp.lastError());
                    failures++;
                } else {
                    String sheetA = prefixA + "-001.otk";
                    String sheetB = prefixB + "-001.otk";

                    // join and unjoin each wipe fileI/fileJ after use, so
                    // each side needs its own copy of the same two starting
                    // sheets.
                    String joinA = dir.file("joinA.otk");
                    String joinB = dir.file("joinB.otk");
                    String unjoinA = dir.file("unjoinA.otk");
                    String unjoinB = dir.file("unjoinB.otk");
                    copyFileOverwrite(sheetA, joinA);
                    copyFileOverwrite(sheetB, joinB);
                    copyFileOverwrite(sheetA, unjoinA);
                    copyFileOverwrite(sheetB, unjoinB);

                    String combinedFile = dir.file("combined.otk");
                    String joinPrefix = dir.file("join_out");
                    String unjoinPrefix = dir.file("unjoin_out");

                    OTP joinOtp = new OTP();
                    if (!joinOtp.joinKeys(joinA, joinB, combinedFile, joinPrefix)) {
                        System.err.println("[FAIL] joinKeys failed: " + joinOtp.lastError());
                        failures++;
                    } else {
                        OTP unjoinOtp = new OTP();
                        if (!unjoinOtp.unjoinKeys(unjoinA, unjoinB, combinedFile, unjoinPrefix)) {
                            System.err.println("[FAIL] unjoinKeys failed: " + unjoinOtp.lastError());
                            failures++;
                        } else {
                            boolean allMatch = true;
                            for (int i = 1; i <= 25; i++) {
                                String buf = String.format("%03d", i);
                                String joinSheet = joinPrefix + "-" + buf + ".otk";
                                String unjoinSheet = unjoinPrefix + "-" + buf + ".otk";

                                String jContent = stripFormatting(readFileRaw(joinSheet));
                                String uContent = stripFormatting(readFileRaw(unjoinSheet));

                                if (jContent.isEmpty() || !jContent.equals(uContent)) {
                                    System.err.println("[FAIL] sheet " + i + " mismatch between join and unjoin output");
                                    allMatch = false;
                                }
                            }
                            if (allMatch) {
                                System.out.println("[PASS] joinKeys / unjoinKeys derived an identical 25-sheet keypad");
                            } else {
                                failures++;
                            }
                        }
                    }
                }
            }

            // ---- 3. splitMessage -> mergeMessage (2-of-4 courier shares) ----
            {
                OTP otp = new OTP();
                String plain = "MEET AT DAWN";
                String prefix = dir.file("splitA-");

                if (!otp.splitMessage(plain, /*minParts=*/2, /*maxParts=*/4, prefix)) {
                    System.err.println("[FAIL] splitMessage failed: " + otp.lastError());
                    failures++;
                } else {
                    // combo(2, 4)'s first group is "12" -- couriers 1 and 2
                    // each hold one share of that group; any 2 of a group's
                    // shares should merge back to the original message.
                    List<String> shareFiles = List.of(prefix + "1-12.otp", prefix + "2-12.otp");
                    Optional<String> merged = otp.mergeMessage(shareFiles);
                    if (merged.isEmpty()) {
                        System.err.println("[FAIL] mergeMessage failed: " + otp.lastError());
                        failures++;
                    } else if (!merged.get().equals(plain)) {
                        System.err.println("[FAIL] split/merge round trip mismatch: sent " + plain + " got " + merged.get());
                        failures++;
                    } else {
                        System.out.println("[PASS] splitMessage -> mergeMessage (2-of-4 courier shares) round trip: " + merged.get());
                    }
                }
            }

            // ---- 4. generateKeyForKnownPlaintext -- repudiation property ----
            // A real key enciphers a real message; a manufactured key should
            // make that SAME ciphertext decipher to a different, chosen
            // plaintext.
            {
                OTP keyGenOtp = new OTP();
                String prefix = dir.file("fakemsgkey");
                keyGenOtp.keygen(prefix, false, 1, false);
                String realKey = prefix + "-001.otk";
                String realKeyCopy = dir.file("fakemsgkey_copy.otk");
                copyFileOverwrite(realKey, realKeyCopy);
                try { Files.deleteIfExists(Paths.get(realKey)); } catch (IOException ignored) { }

                String realPlain = "ATTACK AT DAWN";
                OTP encOtp = new OTP();
                Optional<String> cipher = encOtp.encipher(realPlain, List.of(realKeyCopy));

                if (cipher.isEmpty()) {
                    System.err.println("[FAIL] fakeMsg setup: encipher failed: " + encOtp.lastError());
                    failures++;
                } else {
                    // Cover plaintext must encode to the exact same digit
                    // length as realPlain -- reversing the string guarantees
                    // an identical character multiset (so an identical
                    // encoded length) without having to hand-count
                    // checkerboard digits per letter.
                    String coverPlain = new StringBuilder(realPlain).reverse().toString();
                    OTP fakeOtp = new OTP();
                    String fakeKeyFile = dir.file("fake.otk");
                    boolean genOk = fakeOtp.generateKeyForKnownPlaintext(coverPlain, cipher.get(), fakeKeyFile);
                    if (!genOk) {
                        System.err.println("[FAIL] generateKeyForKnownPlaintext failed: " + fakeOtp.lastError());
                        failures++;
                    } else {
                        OTP checkOtp = new OTP();
                        Optional<String> recovered = checkOtp.decipher(cipher.get(), List.of(fakeKeyFile));
                        if (recovered.isEmpty() || !recovered.get().equals(coverPlain)) {
                            System.err.println("[FAIL] fake key did not reproduce the cover plaintext: got "
                                    + (recovered.isPresent() ? recovered.get() : "<decipher failed>"));
                            failures++;
                        } else {
                            System.out.println("[PASS] generateKeyForKnownPlaintext: same ciphertext deciphers to the chosen cover text: "
                                    + recovered.get());
                        }
                    }
                }
            }

            // ---- 5. combineStreams + wipeFiles ----
            {
                OTP keyGenOtp = new OTP();
                keyGenOtp.keygen(dir.file("streamA"), false, 1, false);
                keyGenOtp.keygen(dir.file("streamB"), false, 1, false);
                String streamA = dir.file("streamA-001.otk");
                String streamB = dir.file("streamB-001.otk");
                String combined = dir.file("combinedStream.otk");

                OTP combineOtp = new OTP();
                if (!combineOtp.combineStreams(streamA, streamB, combined, "")) {
                    System.err.println("[FAIL] combineStreams failed: " + combineOtp.lastError());
                    failures++;
                } else {
                    String combinedDigits = stripFormatting(readFileRaw(combined));
                    if (combinedDigits.length() != OTP.SHEET_SIZE) {
                        System.err.println("[FAIL] combineStreams: expected " + OTP.SHEET_SIZE
                                + " digits, got " + combinedDigits.length());
                        failures++;
                    } else {
                        System.out.println("[PASS] combineStreams produced a " + combinedDigits.length() + "-digit combined stream");
                    }

                    OTP wipeOtp = new OTP();
                    if (!wipeOtp.wipeFiles(List.of(combined)) || Files.exists(Paths.get(combined))) {
                        System.err.println("[FAIL] wipeFiles did not remove: " + combined);
                        failures++;
                    } else {
                        System.out.println("[PASS] wipeFiles removed the combined stream file");
                    }
                }
            }

            if (failures == 0) {
                System.out.println("[INFO] ALL SMOKE TESTS PASSED");
                return 0;
            }
            System.err.println("[FAIL] " + failures + " smoke test(s) FAILED");
            return 1;
        } finally {
            dir.close();
        }
    }
}
