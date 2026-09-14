// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.TreeMap;

// OTP -- core SilentDuck cipher engine. Translated from Kwak's OTP.h/OTP.cpp
// (the standalone, Qt-free C++17 port of otp.py), which was itself
// translated from Quacque's OTP.h/OTP.cpp -- see ../kwek_design.md for the
// full otp.py -> Quacque -> Kwak -> Kwek lineage and the C++ -> Java
// translation table this follows.
//
// Full functional parity with otp.py's/Quacque's/Kwak's cipher/key
// operations: key generation ("-g"/"-gz"/"-kt"), encipher/decipher
// ("-e"/"-d"), key-join/unjoin ("-j"/"-u"), known-plaintext key recovery
// ("-f"), message split/merge ("-s"/"-m"), stream-combine ("-b"), and wipe
// ("-w"). CLI argument parsing and help text are NOT here -- see
// OtpCli.java, the console-facing layer built on this same public API.
//
// Message text is passed in/out as a plain java.lang.String (one Java char
// per codepoint) rather than file paths -- this is Kwek's one deliberate
// departure from a literal C++ -> Java line-for-line translation. Kwak needs
// its own Utf8.h/.cpp because char32_t isn't the platform-portable "one
// codepoint" type C++ hands you for free (see Kwak's Utf8.h comment: wchar_t
// is 16 bits on Windows, 32 on Linux/macOS). Java's char is *always* a UTF-16
// code unit on every platform, and -- as Kwak's own Utf8.h comment already
// establishes -- every character this app's straddling checkerboard actually
// uses (Roman and Cyrillic letters, digits, punctuation) is in the Basic
// Multilingual Plane, i.e. never a surrogate pair. So "one char32_t per
// character" in Kwak and "one char per character" in a plain Java String are
// exactly equivalent here, and no Utf8-equivalent class exists in Kwek: file
// I/O converts straight between String and UTF-8 bytes via
// StandardCharsets.UTF_8 at the point text touches disk, and case-folding
// uses toUpperCustom() below in place of Kwak's utf8::toUpper(). Key
// material stays file-based (.otk files) on purpose, unchanged from every
// earlier port -- keys are bulky, persistent, and meant to live on removable
// media, handed off by courier rather than transmitted.
//
// Every failure path returns null (or Optional.empty() for an
// Optional<String>-returning method) and sets lastError() for the caller to
// show -- never a thrown exception for an ordinary operational failure,
// matching Kwak's (and Quacque's) departure from otp.py's die()/sys.exit()
// here. Kwak's "bool *ok" out-parameter convention (there to let C++ report
// failure without allocating) has no Java equivalent worth keeping: private
// helpers that could fail return null on failure instead, exactly as the
// public API already does, and callers check for null the same way Kwak's
// callers check *ok.
//
// NOTE: the straddling checkerboard's Cyrillic half is built from Java char
// \\uXXXX escapes, never embedded literal Cyrillic source characters -- same
// reasoning as Kwak's OTP.cpp: numeric escapes can't silently corrupt if some
// future edit/diff/clone mangles the source file's encoding.
final class OTP {

    // ---- sheet / pad sizing -- mirrors otp.py's/Quacque's/Kwak's constants ----
    static final int DIGITS_PER_GROUP = 5;
    static final int GROUPS_PER_LINE = 5;
    static final int LINES_PER_PAGE = 10;
    static final int SHEET_SIZE = DIGITS_PER_GROUP * GROUPS_PER_LINE * LINES_PER_PAGE; // 250
    static final int PAGES_PER_PAD = 25;
    static final int PAD_SIZE = SHEET_SIZE * PAGES_PER_PAD; // 6250

    // Historical CIA TRIGON sheet size ("-kt"): 8 groups of 5 digits per
    // line, 40 lines per page -- 1600 digits/page instead of the shorter
    // default above. Only affects keygen(); PAGES_PER_PAD (25) is unchanged.
    static final int TRIGON_GROUPS_PER_LINE = 8;
    static final int TRIGON_LINES_PER_PAGE = 40;
    static final int TRIGON_SHEET_SIZE = DIGITS_PER_GROUP * TRIGON_GROUPS_PER_LINE * TRIGON_LINES_PER_PAGE; // 1600

    private static final int FETCHED_ENTROPY_QUOTA = 25;

    // ---- configuration (mirrors otp.py's/Quacque's/Kwak's settings) ----
    private boolean keepKeyFilesAfterUse = false;
    private int wipeRoundCount = 7;
    private boolean autoInsertDigitShiftCode = true;
    private boolean useMorseShorts = false;
    private boolean testingMode = false;
    private int randomDuplicates = 0;
    private int entropyGatheringSleepTime = 0;
    private int fetchedEntropyCount = 0; // digits fetched since the last entropySleep()

    private String lastErrorMessage = "";

    void setKeepKeyFilesAfterUse(boolean keep) { keepKeyFilesAfterUse = keep; }
    boolean keepKeyFilesAfterUse() { return keepKeyFilesAfterUse; }

    // Number of overwrite passes wipeFile() performs before deleting a key
    // file. Defaults to 7; each pass is forced to physical storage via a real
    // OS-level fsync (FileDescriptor.sync()), not just a buffered write --
    // see wipeFile() below.
    void setWipeRoundCount(int rounds) { wipeRoundCount = rounds; }
    int wipeRoundCount() { return wipeRoundCount; }

    // When false (default/strict), encode() requires an explicit '#'
    // digit-shift code around runs of digits and fails if one is missing.
    // Set true to have encode() insert the missing '#' automatically.
    void setAutoInsertDigitShiftCode(boolean autoInsert) { autoInsertDigitShiftCode = autoInsert; }
    boolean autoInsertDigitShiftCode() { return autoInsertDigitShiftCode; }

    // "-z": substitute Morse-cut letters for digits on the wire. Off by default.
    void setUseMorseShorts(boolean use) { useMorseShorts = use; }
    boolean useMorseShorts() { return useMorseShorts; }

    // "-t": dry-run mode. writeFile()/wipeFile() log what they would have
    // done and return success without touching the filesystem.
    void setTestingMode(boolean testing) { testingMode = testing; }
    boolean testingMode() { return testingMode; }

    // "-q": how many extra rounds of fresh random material get folded into a
    // freshly generated key sheet during keygen() (0 default = one round).
    void setRandomDuplicates(int n) { randomDuplicates = n; }
    int randomDuplicates() { return randomDuplicates; }

    // "-n": sleep this many seconds after every 25 fetched random digits,
    // giving the OS's entropy pool time to refill on constrained hardware.
    // 0 (default) disables it. Blocks the calling thread.
    void setEntropyGatheringSleepTime(int seconds) { entropyGatheringSleepTime = seconds; }
    int entropyGatheringSleepTime() { return entropyGatheringSleepTime; }

    // Message from the most recent call. Cleared at the start of every call
    // below. NOTE: a call can succeed and still leave a message here -- e.g.
    // the crypto succeeded but wiping a spent key file afterward failed.
    // Always check this, not just after a failure.
    String lastError() { return lastErrorMessage; }

    private void clearError() { lastErrorMessage = ""; }
    private void setError(String msg) { lastErrorMessage = msg; }

    // ========================================================================
    // straddling-checkerboard tables -- static and built once at class-load,
    // rather than per-instance in a constructor the way Kwak's buildTables()
    // does: the content never varies between OTP instances, so there is
    // nothing instance-specific to rebuild. See kwek_design.md.
    // ========================================================================

    // number -> Roman letter (single digits, then double digits 70-99).
    private static Map<String, Character> buildNumber2Lat() {
        Map<String, Character> tbl = new LinkedHashMap<>();
        tbl.put("0", 'A'); tbl.put("1", 'E'); tbl.put("2", 'N'); tbl.put("3", 'R');
        tbl.put("4", 'O'); tbl.put("5", 'I'); tbl.put("6", 'T');
        tbl.put("70", 'B'); tbl.put("71", 'C'); tbl.put("72", 'G'); tbl.put("73", 'D'); tbl.put("74", 'F');
        tbl.put("75", 'H'); tbl.put("76", 'J'); tbl.put("77", 'K'); tbl.put("78", 'L'); tbl.put("79", 'M');
        tbl.put("80", 'P'); tbl.put("81", 'Q'); tbl.put("82", 'S'); tbl.put("83", 'U'); tbl.put("84", 'V');
        tbl.put("85", 'W'); tbl.put("86", 'X'); tbl.put("87", 'Y'); tbl.put("88", 'Z');
        // 89 intentionally unassigned -- see otp.py's comment: reserved for a
        // raw-code-plus-length scheme that was never implemented.
        tbl.put("90", '?'); tbl.put("91", ':'); tbl.put("92", '@'); tbl.put("93", '/');
        tbl.put("94", '#'); tbl.put("95", '.'); tbl.put("96", ','); tbl.put("97", '\n');
        tbl.put("98", ' '); tbl.put("99", '~');
        return tbl;
    }

    // number -> Cyrillic letter. Built from \\uXXXX escapes (not embedded
    // literal Cyrillic source characters) for the same file-corruption-
    // resistance reason as Kwak's OTP.cpp -- see this file's top comment.
    // А=0x0410 Б=0x0411 В=0x0412 Г=0x0413 Д=0x0414 Е=0x0415 Ж=0x0416 З=0x0417
    // И=0x0418 Й=0x0419 К=0x041A Л=0x041B М=0x041C Н=0x041D О=0x041E П=0x041F
    // Р=0x0420 С=0x0421 Т=0x0422 У=0x0423 Ф=0x0424 Х=0x0425 Ц=0x0426 Ч=0x0427
    // Ш=0x0428 Щ=0x0429 Ы=0x042B Ь=0x042C Э=0x042D Ю=0x042E Я=0x042F
    private static Map<String, Character> buildNumber2Cyr() {
        Map<String, Character> tbl = new LinkedHashMap<>();
        tbl.put("0", 'А'); tbl.put("1", 'Е'); tbl.put("2", 'И'); tbl.put("3", 'Н');
        tbl.put("4", 'О'); tbl.put("5", 'С'); tbl.put("6", 'Т');
        tbl.put("70", 'Б'); tbl.put("71", 'В'); tbl.put("72", 'Г'); tbl.put("73", 'Д'); tbl.put("74", 'Ж');
        tbl.put("75", 'З'); tbl.put("76", 'Й'); tbl.put("77", 'К'); tbl.put("78", 'Л'); tbl.put("79", 'М');
        tbl.put("80", 'П'); tbl.put("81", 'Р'); tbl.put("82", 'У'); tbl.put("83", 'Ф'); tbl.put("84", 'Х');
        tbl.put("85", 'Ц'); tbl.put("86", 'Ч'); tbl.put("87", 'Ш'); tbl.put("88", 'Щ'); tbl.put("89", 'Ы');
        tbl.put("90", 'Ь'); tbl.put("91", 'Э'); tbl.put("92", 'Ю'); tbl.put("93", 'Я');
        tbl.put("94", '#'); tbl.put("95", '.'); tbl.put("96", ','); tbl.put("97", '\n');
        tbl.put("98", ' '); tbl.put("99", '~');
        return tbl;
    }

    private static Map<Character, String> invert(Map<String, Character> tbl) {
        Map<Character, String> out = new TreeMap<>();
        for (Map.Entry<String, Character> e : tbl.entrySet())
            out.put(e.getValue(), e.getKey());
        return out;
    }

    // TreeMap, not LinkedHashMap: needs sorted iteration to match std::map's
    // guarantee, used by referenceTablesText()'s formatting helpers below
    // (formatCodeLetterTable() relies on sorted-by-code order).
    private static final Map<String, Character> NUMBER2LAT = new TreeMap<>(buildNumber2Lat());
    private static final Map<String, Character> NUMBER2CYR = new TreeMap<>(buildNumber2Cyr());
    private static final Map<Character, String> LAT2NUMBER = invert(NUMBER2LAT);
    private static final Map<Character, String> CYR2NUMBER = invert(NUMBER2CYR);

    // ---- allowed input alphabet ----

    private static String buildAllowedInputChars() {
        String validRoman = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        String validPunct = "?:@/~#., \n";
        String validDigits = "0123456789";
        // Natural Cyrillic reading order (А Б В Г Д Е Ж З И...), since this
        // is meant to be read/pasted as a cheat-sheet -- deliberately NOT the
        // frequency-coded order buildNumber2Cyr() above uses. Same 31 letters
        // either way (the Russian alphabet minus Ё and Ъ).
        char[] cyrillic = {
            0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
            0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
            0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
            0x0428, 0x0429, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
        };
        StringBuilder validCyrillic = new StringBuilder(cyrillic.length);
        for (char c : cyrillic) validCyrillic.append(c);
        return validRoman + validPunct + validDigits + validCyrillic;
    }

    private static final String ALLOWED_INPUT_CHARS = buildAllowedInputChars();

    // The straddling checkerboard's full alphabet, in a fixed display order.
    // Meant for pasting into a future terminal editor when the user can't
    // recall how to type one of its more exotic characters -- see
    // kwek_design.md's note on the deferred EDITOR sentinel.
    static String allowedInputChars() { return ALLOWED_INPUT_CHARS; }

    // True if ch belongs to the straddling checkerboard's alphabet: Roman
    // A-Z, the Cyrillic letters the checkerboard actually uses, digits 0-9,
    // and the punctuation/control chars the checkerboard maps
    // (? : @ / ~ # . , space, newline). Case-insensitive.
    static boolean isAllowedInputChar(char ch) {
        return ALLOWED_INPUT_CHARS.indexOf(toUpperCustom(ch)) >= 0;
    }

    private boolean stringValid(String s) {
        for (int i = 0; i < s.length(); i++) {
            if (!isAllowedInputChar(s.charAt(i)))
                return false;
        }
        return true;
    }

    // Uppercases a single char -- covers exactly the ranges this app's
    // checkerboard alphabet needs (ASCII A-Z and the Cyrillic block used by
    // NUMBER2CYR/LAT2NUMBER), not general Unicode case-folding. Deliberately
    // not Character.toUpperCase(): that follows full Unicode casing rules,
    // which is a broader (and for a couple of edge codepoints, different)
    // mapping than the one every earlier port's utf8::toUpper() implements
    // by hand. Kept identical to Kwak's Utf8.cpp so a message that
    // round-trips through Kwak round-trips through Kwek too.
    private static char toUpperCustom(char ch) {
        if (ch >= 'a' && ch <= 'z')
            return (char) (ch - ('a' - 'A'));
        if (ch >= 0x0430 && ch <= 0x044F)
            return (char) (ch - 0x20);
        return ch;
    }

    private static String toUpperCustom(String s) {
        StringBuilder out = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++)
            out.append(toUpperCustom(s.charAt(i)));
        return out.toString();
    }

    // ========================================================================
    // digit-string arithmetic
    // ========================================================================

    private String stringAddDigits(String a, String b) {
        if (a.length() > b.length()) {
            setError("stringAdd: len(a) > len(b) by " + (a.length() - b.length()) + " characters");
            return null;
        }
        StringBuilder tmp = new StringBuilder(a.length());
        for (int x = 0; x < a.length(); x++) {
            int d = (a.charAt(x) - '0') + (b.charAt(x) - '0');
            if (d > 9) d -= 10;
            tmp.append((char) ('0' + d));
        }
        return tmp.toString();
    }

    private String stringSubtractDigits(String a, String b) {
        if (a.length() > b.length()) {
            setError("stringSubtract: len(a) > len(b) by " + (a.length() - b.length()) + " characters");
            return null;
        }
        StringBuilder tmp = new StringBuilder(a.length());
        for (int x = 0; x < a.length(); x++) {
            int d = (a.charAt(x) - '0') - (b.charAt(x) - '0');
            if (d < 0) d += 10;
            tmp.append((char) ('0' + d));
        }
        return tmp.toString();
    }

    private static String stringDigits(String s) {
        StringBuilder tmp = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch >= '0' && ch <= '9')
                tmp.append(ch);
        }
        return tmp.toString();
    }

    // ========================================================================
    // randomness (Csprng.java -- direct OS CSPRNG, see its header)
    // ========================================================================

    private void entropySleep() {
        // Optionally sleep for a bit every FETCHED_ENTROPY_QUOTA digits, to
        // give the OS's entropy pool time to refill on constrained hardware.
        // A no-op unless setEntropyGatheringSleepTime() has been called.
        if (entropyGatheringSleepTime <= 0)
            return;

        fetchedEntropyCount++;
        if (fetchedEntropyCount > FETCHED_ENTROPY_QUOTA) {
            try {
                Thread.sleep(entropyGatheringSleepTime * 1000L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            fetchedEntropyCount = 0;
        }
    }

    private char randDigit() {
        entropySleep();
        return Csprng.randDigit();
    }

    private String randDigits(int count) {
        StringBuilder tmp = new StringBuilder(count);
        for (int i = 0; i < count; i++)
            tmp.append(randDigit());
        return tmp.toString();
    }

    // ========================================================================
    // formatting
    // ========================================================================

    private static String codeGroups(String s, int groupSize, int groupsPerLine, int linesPerPage) {
        StringBuilder tmp = new StringBuilder();
        int lineCount = 0;
        int groupCount = 0;

        for (int x = 0; x < s.length(); x += groupSize) {
            tmp.append(s, x, Math.min(x + groupSize, s.length())).append(' ');
            groupCount++;

            if (groupCount >= groupsPerLine) {
                tmp.append('\n');
                groupCount = 0;
                lineCount++;

                if (lineCount >= linesPerPage) {
                    tmp.append('\n');
                    lineCount = 0;
                }
            }
        }
        return tmp.toString();
    }

    private static String codeGroups(String s) {
        return codeGroups(s, DIGITS_PER_GROUP, GROUPS_PER_LINE, LINES_PER_PAGE);
    }

    // ========================================================================
    // Morse-cut substitution (off unless setUseMorseShorts(true))
    // ========================================================================

    private static Map<Character, Character> buildMorseCutLetter() {
        Map<Character, Character> tbl = new TreeMap<>();
        tbl.put('0', 'T'); tbl.put('1', 'A'); tbl.put('2', 'U'); tbl.put('3', 'V'); tbl.put('4', '4');
        tbl.put('5', 'E'); tbl.put('6', '6'); tbl.put('7', 'B'); tbl.put('8', 'D'); tbl.put('9', 'N');
        return tbl;
    }

    private static Map<Character, Character> buildMorseCutNumber() {
        Map<Character, Character> tbl = new TreeMap<>();
        tbl.put('T', '0'); tbl.put('A', '1'); tbl.put('U', '2'); tbl.put('V', '3'); tbl.put('4', '4');
        tbl.put('E', '5'); tbl.put('6', '6'); tbl.put('B', '7'); tbl.put('D', '8'); tbl.put('N', '9');
        return tbl;
    }

    private static final Map<Character, Character> MORSE_CUT_LETTER = buildMorseCutLetter();
    private static final Map<Character, Character> MORSE_CUT_NUMBER = buildMorseCutNumber();

    private String toMorseCut(String s) {
        if (!useMorseShorts)
            return s;
        StringBuilder tmp = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch == ' ' || ch == '\n')
                continue;
            Character mapped = MORSE_CUT_LETTER.get(ch);
            tmp.append(mapped != null ? mapped : '?');
        }
        return tmp.toString();
    }

    private String fromMorseCut(String s) {
        if (!useMorseShorts)
            return s;
        StringBuilder tmp = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            char up = (ch >= 'a' && ch <= 'z') ? (char) (ch - ('a' - 'A')) : ch;
            if (up == ' ' || up == '\n')
                continue;
            Character mapped = MORSE_CUT_NUMBER.get(up);
            tmp.append(mapped != null ? mapped : '?');
        }
        return tmp.toString();
    }

    // ========================================================================
    // reference tables -- for the "-hh" full help text (see OtpCli.java's
    // printFullHelp(), not yet written -- see kwek_design.md). Mirrors
    // otp.py's/Quacque's/Kwak's appended "REFERENCE TABLES" section: the
    // straddling-checkerboard digit-code<->letter tables (Roman and
    // Cyrillic), plus the Morse cut shorts substitution, for quick lookup
    // while working through a message by hand.
    // ========================================================================

    // give the unprintable/whitespace table entries a readable name (a
    // literal space or newline is invisible in a table cell otherwise).
    private static String displayChar(char ch) {
        if (ch == '\n') return "<newline>";
        if (ch == ' ') return "<space>";
        return String.valueOf(ch);
    }

    // Right-justify a (always ASCII, 1-2 digit) checkerboard code to width 2.
    private static String padCode2(String code) {
        return code.length() < 2 ? (" " + code) : code;
    }

    // "code = letter", sorted by code -- NUMBER2LAT/NUMBER2CYR are TreeMaps,
    // which iterate in string-sorted order; for these tables' codes
    // ("0".."6" then "70".."99") that already matches the intended
    // single-digits-before-double-digits, then-numeric order (every
    // double-digit code's first character is '7'-'9', which sorts after
    // every single-digit code's only character).
    private static String formatCodeLetterTable(Map<String, Character> number2letter, int columns) {
        StringBuilder out = new StringBuilder();
        List<String> row = new ArrayList<>();
        for (Map.Entry<String, Character> e : number2letter.entrySet()) {
            row.add(padCode2(e.getKey()) + " = " + displayChar(e.getValue()));
            if (row.size() == columns) {
                out.append(String.join("  ", row)).append('\n');
                row.clear();
            }
        }
        if (!row.isEmpty())
            out.append(String.join("  ", row)).append('\n');
        return out.toString();
    }

    // "letter = code" -- the inverse of formatCodeLetterTable() above, sorted
    // by codepoint (a TreeMap<Character,...> iterates in numeric key order).
    private static String formatLetterCodeTable(Map<String, Character> number2letter, int columns) {
        Map<Character, String> letter2number = invert(number2letter);
        StringBuilder out = new StringBuilder();
        List<String> row = new ArrayList<>();
        for (Map.Entry<Character, String> e : letter2number.entrySet()) {
            row.add(displayChar(e.getKey()) + " = " + padCode2(e.getValue()));
            if (row.size() == columns) {
                out.append(String.join("  ", row)).append('\n');
                row.clear();
            }
        }
        if (!row.isEmpty())
            out.append(String.join("  ", row)).append('\n');
        return out.toString();
    }

    // "key = value" for the Morse cut shorts tables -- works for either
    // direction, since MORSE_CUT_LETTER and MORSE_CUT_NUMBER are both
    // Map<Character,Character>.
    private static String formatMorseTable(Map<Character, Character> tbl, int columns) {
        StringBuilder out = new StringBuilder();
        List<String> row = new ArrayList<>();
        for (Map.Entry<Character, Character> e : tbl.entrySet()) {
            row.add(e.getKey() + " = " + e.getValue());
            if (row.size() == columns) {
                out.append(String.join("  ", row)).append('\n');
                row.clear();
            }
        }
        if (!row.isEmpty())
            out.append(String.join("  ", row)).append('\n');
        return out.toString();
    }

    // Standard International Morse Code, reference only -- covers the ten
    // digits plus the specific eight letters buildMorseCutLetter()
    // substitutes in for a digit ('4' and '6' are left as themselves, so no
    // letter entry is needed for those). Not used by toMorseCut()/
    // fromMorseCut() above, which only ever substitute one character for
    // another and never touch actual dot/dash sequences -- this exists
    // purely so formatMorseToCutTable() below can show why the substitution
    // saves time on the wire (every digit's own Morse code is a full five
    // symbols; its cut substitute is often shorter).
    private static Map<Character, String> buildMorseCodeDigits() {
        Map<Character, String> tbl = new TreeMap<>();
        tbl.put('0', "-----"); tbl.put('1', ".----"); tbl.put('2', "..---"); tbl.put('3', "...--"); tbl.put('4', "....-");
        tbl.put('5', "....."); tbl.put('6', "-...."); tbl.put('7', "--..."); tbl.put('8', "---.."); tbl.put('9', "----.");
        return tbl;
    }

    private static Map<Character, String> buildMorseCodeLetters() {
        Map<Character, String> tbl = new TreeMap<>();
        tbl.put('A', ".-"); tbl.put('B', "-..."); tbl.put('D', "-.."); tbl.put('E', ".");
        tbl.put('N', "-."); tbl.put('T', "-"); tbl.put('U', "..-"); tbl.put('V', "...-");
        return tbl;
    }

    private static final Map<Character, String> MORSE_CODE_DIGITS = buildMorseCodeDigits();
    private static final Map<Character, String> MORSE_CODE_LETTERS = buildMorseCodeLetters();

    private static String padRight(String s, int width) {
        return s.length() < width ? s + " ".repeat(width - s.length()) : s;
    }

    // render the "digit's own Morse code -> Morse cut shorts letter's
    // (shorter) Morse code" table, showing at a glance why each substitution
    // is worth it.
    private static String formatMorseToCutTable() {
        StringBuilder out = new StringBuilder();
        out.append(padRight("Digit", 5)).append(' ').append(padRight("Morse", 7)).append(' ')
           .append(padRight("Cut", 4)).append(' ').append("Cut Morse").append('\n')
           .append(padRight("-----", 5)).append(' ').append(padRight("-----", 7)).append(' ')
           .append(padRight("---", 4)).append(' ').append("---------").append('\n');

        for (Map.Entry<Character, Character> e : MORSE_CUT_LETTER.entrySet()) {
            char digit = e.getKey();
            char cutLetter = e.getValue();
            String cutMorse = MORSE_CODE_LETTERS.containsKey(cutLetter)
                ? MORSE_CODE_LETTERS.get(cutLetter) : MORSE_CODE_DIGITS.get(cutLetter);
            out.append(padRight(String.valueOf(digit), 5)).append(' ')
               .append(padRight(MORSE_CODE_DIGITS.get(digit), 7)).append(' ')
               .append(padRight(String.valueOf(cutLetter), 4)).append(' ')
               .append(cutMorse).append('\n');
        }
        return out.toString();
    }

    static String referenceTablesText() {
        StringBuilder out = new StringBuilder();
        out.append("\n")
           .append("*********************************************************\n")
           .append("* REFERENCE TABLES                                      *\n")
           .append("*********************************************************\n")
           .append("\n")
           .append("These are the straddling-checkerboard digit-code tables used by encode()/decode()\n")
           .append("(Roman and Cyrillic), plus the Morse cut shorts substitution used when \"-z\" is on.\n")
           .append("Kept here for quick lookup while working through a message by hand.\n")
           .append("\n")
           .append("Roman: digit-code -> letter\n")
           .append(formatCodeLetterTable(NUMBER2LAT, 5))
           .append("\nRoman: letter -> digit-code\n")
           .append(formatLetterCodeTable(NUMBER2LAT, 5))
           .append("\nCyrillic: digit-code -> letter\n")
           .append(formatCodeLetterTable(NUMBER2CYR, 5))
           .append("\nCyrillic: letter -> digit-code\n")
           .append(formatLetterCodeTable(NUMBER2CYR, 5))
           .append("\nMorse cut shorts: digit -> letter\n")
           .append(formatMorseTable(MORSE_CUT_LETTER, 5))
           .append("\nMorse cut shorts: letter -> digit\n")
           .append(formatMorseTable(MORSE_CUT_NUMBER, 5))
           .append("\nMorse -> Morse cut shorts (each digit's own Morse code vs. its shorter substitute)\n")
           .append(formatMorseToCutTable());
        return out.toString();
    }

    // ========================================================================
    // checkerboard encode/decode -- kept as a near-literal transliteration of
    // Kwak's (and Quacque's, and otp.py's) index arithmetic (see decode()
    // especially): this is exactly the kind of code where a "cleaner"
    // rewrite risks a subtle off-by-one, so the same control flow/increments
    // are kept rather than restructured.
    // ========================================================================

    private String insertAlphabetSwitches(String s) {
        StringBuilder tmp = new StringBuilder(s.length());
        boolean usingRoman = true;

        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch == '~') {
                usingRoman = !usingRoman;
                tmp.append(ch);
                continue;
            }

            boolean inLat = LAT2NUMBER.containsKey(ch);
            boolean inCyr = CYR2NUMBER.containsKey(ch);

            if (inLat && !inCyr && !usingRoman) {
                tmp.append('~');
                usingRoman = true;
            } else if (inCyr && !inLat && usingRoman) {
                tmp.append('~');
                usingRoman = false;
            }

            tmp.append(ch);
        }
        return tmp.toString();
    }

    private String encode(String plain) {
        boolean usingRoman = true;
        boolean usingDigits = false;
        String s = toUpperCustom(plain);

        if (!stringValid(s)) {
            setError("invalid characters in string: " + s);
            return null;
        }

        s = insertAlphabetSwitches(s);
        int lenS = s.length();
        StringBuilder tmp = new StringBuilder();

        Map<Character, String> currentTbl = LAT2NUMBER;

        int x = 0;
        while (x < lenS) {
            char ch = s.charAt(x);

            if (ch == '#') {
                usingDigits = !usingDigits;
                tmp.append("94");
            } else if (ch == '~') {
                tmp.append("99");
                usingRoman = !usingRoman;
                currentTbl = usingRoman ? LAT2NUMBER : CYR2NUMBER;
            } else if (ch >= '0' && ch <= '9') {
                if (!usingDigits) {
                    if (autoInsertDigitShiftCode) {
                        usingDigits = true;
                        tmp.append("94");
                    } else {
                        setError("missing digit code '#' at start of number sequence");
                        return null;
                    }
                }
                char digitChar = (char) ('0' + (ch - '0'));
                tmp.append(digitChar).append(digitChar);
            } else {
                if (usingDigits) {
                    if (autoInsertDigitShiftCode) {
                        usingDigits = false;
                        tmp.append("94");
                    } else {
                        setError("missing '#' code to end number sequence");
                        return null;
                    }
                }

                String code = currentTbl.get(ch);
                if (code == null) {
                    String alphabetName = usingRoman ? "Roman" : "Cyrillic";
                    setError("letter '" + ch + "' not in the " + alphabetName + " alphabet - missing '~'?");
                    return null;
                }
                tmp.append(code);
            }
            x++;
        }

        if (usingDigits) {
            if (autoInsertDigitShiftCode) {
                tmp.append("94");
            } else {
                setError("missing final '#' code to end number sequence");
                return null;
            }
        }

        if (!usingRoman)
            tmp.append("99");

        return tmp.toString();
    }

    private String decode(String digits) {
        String s = stringDigits(digits);
        int lenS = s.length();
        boolean usingRoman = true;
        StringBuilder tmp = new StringBuilder();
        String code;
        int x = 0;

        Map<String, Character> number2letter = NUMBER2LAT;

        while (x < lenS) {
            code = s.substring(x, x + 1);
            if (code.compareTo("6") > 0) {
                // double-digit code
                x++;
                if (x < lenS) {
                    code += s.charAt(x);
                } else {
                    setError("string missing a character while decoding: " + digits);
                    return null;
                }
            }

            if (code.equals("94")) {
                // '#' is a control code, not part of the message -- tracked
                // here but not written to the output, same as '99' below, so
                // decoded text comes back clean without the caller having to
                // strip it.
                x++;
                code = "";
                while (!code.equals("94")) {
                    if (lenS < x + 2) {
                        setError("numbers are short a digit: " + digits);
                        return null;
                    }
                    code = s.substring(x, x + 2);

                    // all ten "number" codes are a digit doubled ("00".."99");
                    // anything else here is either the '94' shift-close or an error.
                    if (code.length() == 2 && code.charAt(0) == code.charAt(1)
                            && code.charAt(0) >= '0' && code.charAt(0) <= '9') {
                        tmp.append(code.charAt(0));
                    } else if (code.equals("94")) {
                        x -= 1;
                    } else {
                        setError("error decoding digit run: " + digits);
                        return null;
                    }
                    x += 2;
                }
            } else if (code.equals("99")) {
                usingRoman = !usingRoman;
                number2letter = usingRoman ? NUMBER2LAT : NUMBER2CYR;
            } else {
                Character letter = number2letter.get(code);
                if (letter == null) {
                    setError("code '" + code + "' is not assigned to any letter in this alphabet: " + digits);
                    return null;
                }
                tmp.append(letter.charValue());
            }

            x++;
        }

        return tmp.toString();
    }

    // ========================================================================
    // key-file I/O (UTF-8 bytes on disk, same as otp.py/Quacque/Kwak)
    // ========================================================================

    private String readFile(String fn) {
        try {
            byte[] bytes = Files.readAllBytes(Paths.get(fn));
            return new String(bytes, StandardCharsets.UTF_8);
        } catch (IOException e) {
            setError("error reading file: " + fn);
            return null;
        }
    }

    private boolean writeFile(String fn, String s) {
        if (testingMode) {
            setError("TESTING MODE: writeFile() did not write: " + fn);
            return true; // logged via lastError(), not a failure -- matches otp.py's dbg()-and-return
        }

        Path p = Paths.get(fn);
        Path parent = p.toAbsolutePath().getParent();
        if (parent != null) {
            try {
                Files.createDirectories(parent);
            } catch (IOException e) {
                setError("error creating directory: " + parent);
                return false;
            }
        }

        // FileOutputStream + FileDescriptor.sync() forces the write past the
        // OS page cache to physical storage on every platform this targets
        // in one portable call -- the direct equivalent of Kwak's
        // forceFlushToDisk(), which needs its own #ifdef'd
        // FlushFileBuffers()/fsync() to get the same guarantee in C++. See
        // kwek_design.md.
        try (FileOutputStream fos = new FileOutputStream(fn)) {
            fos.write(s.getBytes(StandardCharsets.UTF_8));
            fos.flush();
            fos.getFD().sync();
        } catch (IOException e) {
            setError("error writing file: " + fn);
            return false;
        }
        return true;
    }

    private String loadKeyPad(String fn) {
        String raw = readFile(fn);
        if (raw == null)
            return null;
        return stringDigits(raw);
    }

    private String loadKeys(List<String> keyFiles) {
        StringBuilder key = new StringBuilder();
        for (String kfn : keyFiles) {
            String content = readFile(kfn);
            if (content == null)
                return null;
            key.append(content);
        }
        return stringDigits(key.toString());
    }

    // all ones, all zeroes, a spread of hex patterns, then zeroes again --
    // same pattern list as otp.py's/Quacque's/Kwak's wipeFile().
    private static final byte[] WIPE_PATTERNS = {
        (byte) 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        (byte) 0x88, (byte) 0x99, (byte) 0xaa, (byte) 0xbb, (byte) 0xcc, (byte) 0xdd, (byte) 0xee, (byte) 0xff, 0x00,
    };

    private boolean wipeFile(String fn) {
        if (testingMode) {
            setError("TESTING MODE: wipeFile() did not execute: " + fn);
            return true;
        }

        long fileSize;
        try {
            fileSize = Files.size(Paths.get(fn));
        } catch (IOException e) {
            setError("error opening file to wipe: " + fn);
            return false;
        }

        try (RandomAccessFile raf = new RandomAccessFile(fn, "rw")) {
            byte[] buf = new byte[(int) fileSize];
            for (int r = 0; r < wipeRoundCount; r++) {
                for (byte patternByte : WIPE_PATTERNS) {
                    Arrays.fill(buf, patternByte);
                    raf.seek(0);
                    raf.write(buf);
                    raf.getFD().sync();
                }
            }
        } catch (IOException e) {
            setError("error opening file to wipe: " + fn);
            return false;
        }

        try {
            Files.delete(Paths.get(fn));
        } catch (IOException e) {
            setError("error removing wiped file: " + fn);
            return false;
        }
        return true;
    }

    private boolean wipeKeys(List<String> keyFiles) {
        if (keepKeyFilesAfterUse) {
            setError("warning: key files kept after use (keepKeyFilesAfterUse is set)");
            return true;
        }

        // Unlike otp.py (which die()s the whole process on the first wipe
        // failure, potentially leaving later key files un-wiped), keep going
        // through every file and report the last failure -- matches Kwak's
        // own departure from otp.py here: more of the spent keys end up
        // actually wiped this way, which matters more than which exact
        // error message survives.
        boolean allOk = true;
        for (String kfn : keyFiles) {
            if (!wipeFile(kfn))
                allOk = false;
        }
        return allOk;
    }

    // ========================================================================
    // key generation
    // ========================================================================

    private static String zeroPad(int value, int width) {
        String s = Integer.toString(value);
        if (s.length() < width)
            s = "0".repeat(width - s.length()) + s;
        return s;
    }

    // "-g" / "-gz" / "-kt": writes pagesPerPad .otk files named
    // "<prefix>-NNN.otk" (NNN 001..pagesPerPad), each one sheet (SHEET_SIZE
    // digits) of key material. zeroKeys=true generates an all-zero pad --
    // testing only, NEVER for a real message. trigon=true ("-kt") writes
    // TRIGON_SHEET_SIZE-digit sheets instead, matching the historical CIA
    // TRIGON page size.
    boolean keygen(String prefix, boolean zeroKeys, int pagesPerPad, boolean trigon) {
        clearError();
        if (prefix.isEmpty()) {
            setError("no key prefix given");
            return false;
        }

        final int groupsPerLine = trigon ? TRIGON_GROUPS_PER_LINE : GROUPS_PER_LINE;
        final int linesPerPage = trigon ? TRIGON_LINES_PER_PAGE : LINES_PER_PAGE;
        final int sheetSize = trigon ? TRIGON_SHEET_SIZE : SHEET_SIZE;

        for (int x = 1; x <= pagesPerPad; x++) {
            String fn = prefix + "-" + zeroPad(x, 3) + ".otk";

            String tmp;
            if (zeroKeys) {
                tmp = "0".repeat(sheetSize);
            } else {
                // Folds (1 + randomDuplicates) fresh random draws together via
                // stringAddDigits() rather than taking just one -- defaults
                // to one draw unless the caller (via "-q") asked for extra
                // whitening rounds.
                tmp = "0".repeat(sheetSize);
                for (int i = 0; i < 1 + randomDuplicates; i++) {
                    tmp = stringAddDigits(tmp, randDigits(sheetSize));
                    if (tmp == null)
                        return false;
                }
            }

            if (!writeFile(fn, codeGroups(tmp, DIGITS_PER_GROUP, groupsPerLine, linesPerPage)))
                return false;
        }
        return true;
    }

    boolean keygen(String prefix) { return keygen(prefix, false, PAGES_PER_PAD, false); }
    boolean keygen(String prefix, boolean zeroKeys) { return keygen(prefix, zeroKeys, PAGES_PER_PAD, false); }
    boolean keygen(String prefix, boolean zeroKeys, int pagesPerPad) { return keygen(prefix, zeroKeys, pagesPerPad, false); }

    // ========================================================================
    // encipher / decipher
    // ========================================================================

    // keyFiles is one or more .otk paths, concatenated in order. Spent key
    // files are wiped (unless keepKeyFilesAfterUse()) after a successful
    // call. Returns Optional.empty() on failure -- check lastError().
    Optional<String> encipher(String plainText, List<String> keyFiles) {
        clearError();

        String key = loadKeys(keyFiles);
        if (key == null)
            return Optional.empty();

        String inputTxt = toUpperCustom(plainText);
        String encodedTxt = encode(inputTxt);
        if (encodedTxt == null)
            return Optional.empty();

        if (encodedTxt.length() > key.length()) {
            setError("not enough key material: message needs " + encodedTxt.length()
                    + " digits, key has " + key.length());
            return Optional.empty();
        }

        // Deliberately encodedTxt - key (not key - encodedTxt): this is what
        // lets a message prefixed with "AAAAA" (-> "00000") double as an
        // easy-to-eyeball confirmation of which keypad was used.
        String cipherTxt = stringSubtractDigits(encodedTxt, key);
        if (cipherTxt == null)
            return Optional.empty();

        String result = codeGroups(toMorseCut(cipherTxt));

        // Crypto succeeded -- wipe the spent keys, but a wipe failure is a
        // secondary (if security-relevant) concern, not a reason to withhold
        // the already-correct ciphertext. See lastError()'s doc comment.
        String cryptoError = "";
        if (!wipeKeys(keyFiles))
            cryptoError = lastErrorMessage;

        if (!cryptoError.isEmpty())
            setError(cryptoError);
        else
            clearError();

        return Optional.of(result);
    }

    Optional<String> decipher(String cipherText, List<String> keyFiles) {
        clearError();

        String key = loadKeys(keyFiles);
        if (key == null)
            return Optional.empty();

        String inputTxt = stringDigits(fromMorseCut(cipherText));

        if (inputTxt.length() > key.length()) {
            setError("not enough key material: ciphertext needs " + inputTxt.length()
                    + " digits, key has " + key.length());
            return Optional.empty();
        }

        String clearTxt = stringAddDigits(inputTxt, key);
        if (clearTxt == null)
            return Optional.empty();

        String decodedTxt = decode(clearTxt);
        if (decodedTxt == null)
            return Optional.empty();

        String cryptoError = "";
        if (!wipeKeys(keyFiles))
            cryptoError = lastErrorMessage;

        if (!cryptoError.isEmpty())
            setError(cryptoError);
        else
            clearError();

        return Optional.of(decodedTxt);
    }

    // ========================================================================
    // key join / unjoin
    // ========================================================================

    // All 5-of-10 combinations of digits 0-9, in lexicographic order --
    // "01234", "01235", ..., "56789" -- 252 entries. Generated once here
    // rather than transcribed as a 252-entry literal table -- this generates
    // the identical order because it's the same lexicographic walk Python's
    // itertools.combinations() (and Kwak's own combo5x10()) takes.
    private static List<String> buildCombo5x10() {
        List<String> t = new ArrayList<>(252);
        for (int a = 0; a <= 5; a++)
        for (int b = a + 1; b <= 6; b++)
        for (int c = b + 1; c <= 7; c++)
        for (int d = c + 1; d <= 8; d++)
        for (int e = d + 1; e <= 9; e++) {
            t.add("" + (char) ('0' + a) + (char) ('0' + b) + (char) ('0' + c) + (char) ('0' + d) + (char) ('0' + e));
        }
        return t;
    }

    private static final List<String> COMBO_5X10 = buildCombo5x10();

    private List<String> combinateExpandedKeys(String keyInput) {
        String[] rows = new String[10];
        for (int d = 0; d < 10; d++)
            rows[d] = keyInput.substring(d * 25, d * 25 + 25);

        List<String> retVal = new ArrayList<>(252);

        for (String combo : COMBO_5X10) {
            String s = "0".repeat(25);
            for (int i = 0; i < combo.length(); i++) {
                int digit = combo.charAt(i) - '0';
                s = stringSubtractDigits(s, rows[digit]);
                if (s == null)
                    return null;
            }

            String checkSum = s.substring(0, 5);
            checkSum = stringAddDigits(checkSum, s.substring(5, 10));
            if (checkSum == null) return null;
            checkSum = stringAddDigits(checkSum, s.substring(10, 15));
            if (checkSum == null) return null;
            checkSum = stringAddDigits(checkSum, s.substring(15, 20));
            if (checkSum == null) return null;
            checkSum = stringAddDigits(checkSum, s.substring(20, 25));
            if (checkSum == null) return null;

            retVal.add(s + checkSum);
        }
        return retVal;
    }

    // joinKeys() and unjoinKeys() share everything up through recovering the
    // 504-row, checksum-sorted, halved key table -- identical in both
    // directions. Only what happens with the two halves afterward differs:
    // join masks them with fresh randomness and emits both; unjoin recovers
    // that same randomness from an already-received combinedKeyFile.

    // Combine two SHEET_SIZE-digit key sheets (fileI, fileJ) into a fresh
    // 25-sheet keypad. combinedKeyFile is the in-the-clear table exchanged
    // between the join side and the unjoin side so both derive the same new
    // keypad without ever putting the new keypad itself on the wire.
    // fileI/fileJ are wiped after use (unless keepKeyFilesAfterUse()). Both
    // sides write the 25 new sheets as "<prefix>-NNN.otk".
    boolean joinKeys(String fileI, String fileJ, String combinedKeyFile, String prefix) {
        clearError();
        if (prefix.isEmpty()) {
            setError("no key prefix given");
            return false;
        }

        String ki = loadKeyPad(fileI);
        if (ki == null || ki.length() != SHEET_SIZE) {
            setError("bad first key (expected " + SHEET_SIZE + " digits, got " + (ki == null ? 0 : ki.length()) + ")");
            return false;
        }
        String kj = loadKeyPad(fileJ);
        if (kj == null || kj.length() != SHEET_SIZE) {
            setError("bad second key (expected " + SHEET_SIZE + " digits, got " + (kj == null ? 0 : kj.length()) + ")");
            return false;
        }

        List<String> iTmp = combinateExpandedKeys(ki);
        if (iTmp == null) return false;
        List<String> jTmp = combinateExpandedKeys(kj);
        if (jTmp == null) return false;

        List<String> tmp = new ArrayList<>(iTmp);
        tmp.addAll(jTmp); // 504 rows of 30 digits (25 + 5 checksum)

        // index by the trailing 5-digit checksum, resolving collisions by
        // walking forward mod 100000 -- must match unjoinKeys()'s walk exactly.
        Map<String, String> keys = new TreeMap<>();
        for (String row : tmp) {
            String checkSum = row.substring(row.length() - 5);
            String value = row.substring(0, row.length() - 5);

            while (keys.containsKey(checkSum)) {
                int csNum = (Integer.parseInt(checkSum) + 1) % 100000;
                checkSum = zeroPad(csNum, 5);
            }
            keys.put(checkSum, value);
        }

        // a TreeMap keeps keys sorted already, so walking its values() is
        // exactly Python's/Kwak's explicit "for k in sorted(keys.keys())" walk.
        List<String> sortedVals = new ArrayList<>(keys.values());

        int half = sortedVals.size() / 2;
        List<String> keysA = sortedVals.subList(0, half);
        List<String> keysB = sortedVals.subList(half, sortedVals.size());

        StringBuilder newOtpTbl = new StringBuilder();
        StringBuilder newRandomKeyStr = new StringBuilder();

        for (int x = 0; x < half; x++) {
            String tmpS = stringAddDigits(keysA.get(x), keysB.get(x));
            if (tmpS == null) return false;

            // freshly generated random pad material -- this, not the
            // ciphertext below, is the new keypad delivered locally in the
            // clear further down.
            String rd = randDigits(25);
            newRandomKeyStr.append(rd);

            tmpS = stringSubtractDigits(tmpS, rd);
            if (tmpS == null) return false;
            newOtpTbl.append(tmpS);
        }

        // combinedKeyFile: sent to the distant recipient, who reconstructs
        // newRandomKeyStr from it via unjoinKeys().
        if (!writeFile(combinedKeyFile, codeGroups(newOtpTbl.toString())))
            return false;

        // NOTE (carried over from otp.py/Quacque/Kwak, not fixed here):
        // newRandomKeyStr is half*25 digits (6300 for the default 252-row
        // half), but only the first PAD_SIZE (6250) digits get written out
        // below as the 25 new sheets -- the trailing ~50 digits of freshly
        // generated randomness are computed, folded into newOtpTbl above,
        // and then simply never saved by either side. Both joinKeys() and
        // unjoinKeys() apply the same truncation, so the two sides stay in
        // sync; it just means a little of the generated entropy goes unused
        // rather than becoming key material. See kwek_design.md.
        for (int i = 0; i < PAGES_PER_PAD; i++) {
            String tmpStr = newRandomKeyStr.substring(i * SHEET_SIZE, i * SHEET_SIZE + SHEET_SIZE);
            String filename = prefix + "-" + zeroPad(i + 1, 3) + ".otk";
            if (!writeFile(filename, codeGroups(tmpStr)))
                return false;
        }

        String wipeError = "";
        if (!wipeKeys(Arrays.asList(fileI, fileJ)))
            wipeError = lastErrorMessage;
        if (!wipeError.isEmpty())
            setError(wipeError);
        else
            clearError();

        return true;
    }

    boolean unjoinKeys(String fileI, String fileJ, String combinedKeyFile, String prefix) {
        clearError();
        if (prefix.isEmpty()) {
            setError("no key prefix given");
            return false;
        }

        String ki = loadKeyPad(fileI);
        if (ki == null || ki.length() != SHEET_SIZE) {
            setError("bad first key (expected " + SHEET_SIZE + " digits, got " + (ki == null ? 0 : ki.length()) + ")");
            return false;
        }
        String kj = loadKeyPad(fileJ);
        if (kj == null || kj.length() != SHEET_SIZE) {
            setError("bad second key (expected " + SHEET_SIZE + " digits, got " + (kj == null ? 0 : kj.length()) + ")");
            return false;
        }

        List<String> iTmp = combinateExpandedKeys(ki);
        if (iTmp == null) return false;
        List<String> jTmp = combinateExpandedKeys(kj);
        if (jTmp == null) return false;

        List<String> tmp = new ArrayList<>(iTmp);
        tmp.addAll(jTmp);

        Map<String, String> keys = new TreeMap<>();
        for (String row : tmp) {
            String checkSum = row.substring(row.length() - 5);
            String value = row.substring(0, row.length() - 5);

            while (keys.containsKey(checkSum)) {
                int csNum = (Integer.parseInt(checkSum) + 1) % 100000;
                checkSum = zeroPad(csNum, 5);
            }
            keys.put(checkSum, value);
        }

        List<String> sortedVals = new ArrayList<>(keys.values());

        int half = sortedVals.size() / 2;
        List<String> keysA = sortedVals.subList(0, half);
        List<String> keysB = sortedVals.subList(half, sortedVals.size());

        StringBuilder combinedKeys = new StringBuilder();
        for (int x = 0; x < half; x++) {
            String added = stringAddDigits(keysA.get(x), keysB.get(x));
            if (added == null) return false;
            combinedKeys.append(added);
        }

        String keyInput = loadKeyPad(combinedKeyFile);
        if (keyInput == null) return false;
        if (keyInput.length() != combinedKeys.length()) {
            setError("combined key file length does not match the expected keypad length");
            return false;
        }

        // invert joinKeys()'s masking step: ct = K - rd, so rd = K - ct
        String newRandomKeyStr = stringSubtractDigits(combinedKeys.toString(), keyInput);
        if (newRandomKeyStr == null) return false;

        for (int i = 0; i < PAGES_PER_PAD; i++) {
            String tmpStr = newRandomKeyStr.substring(i * SHEET_SIZE, i * SHEET_SIZE + SHEET_SIZE);
            String filename = prefix + "-" + zeroPad(i + 1, 3) + ".otk";
            if (!writeFile(filename, codeGroups(tmpStr)))
                return false;
        }

        String wipeError = "";
        if (!wipeKeys(Arrays.asList(fileI, fileJ)))
            wipeError = lastErrorMessage;
        if (!wipeError.isEmpty())
            setError(wipeError);
        else
            clearError();

        return true;
    }

    // ========================================================================
    // known-plaintext key recovery ("-f")
    // ========================================================================

    // Given a plaintext message and the ciphertext it's known to correspond
    // to, computes the key that would produce that exact pairing and writes
    // it to keyOutputFile. Deliberate feature, not a bug: repudiation -- any
    // OTP ciphertext can be "decrypted" to any plaintext of the same length,
    // given a manufactured key to match.
    boolean generateKeyForKnownPlaintext(String plainText, String cipherText, String keyOutputFile) {
        clearError();

        String msgP = encode(plainText);
        if (msgP == null)
            return false;

        String msgC = stringDigits(fromMorseCut(cipherText));

        if (msgC.length() != msgP.length()) {
            setError("ciphertext and encoded plaintext must be the same length "
                    + "(ciphertext: " + msgC.length() + " digits, plaintext: " + msgP.length() + " digits)");
            return false;
        }

        String kStr = stringSubtractDigits(msgP, msgC);
        if (kStr == null)
            return false;

        return writeFile(keyOutputFile, codeGroups(kStr));
    }

    // ========================================================================
    // general m-of-n combinations, message split / merge
    // ========================================================================

    // Standard lexicographic "next combination" walk over {1..n} choose m --
    // produces the same order as Python's itertools.combinations(range(1,
    // n+1), m) (and Kwak's own combo()).
    private static List<String> combo(int m, int n) {
        List<String> result = new ArrayList<>();
        if (m <= 0 || m > n)
            return result;

        int[] idx = new int[m];
        for (int i = 0; i < m; i++)
            idx[i] = i; // 0-based; courier number is idx[i] + 1

        while (true) {
            StringBuilder s = new StringBuilder();
            for (int i = 0; i < m; i++)
                s.append(idx[i] + 1);
            result.add(s.toString());

            int i = m - 1;
            while (i >= 0 && idx[i] == n - m + i)
                i--;
            if (i < 0)
                break; // that was the last combination
            idx[i]++;
            for (int j = i + 1; j < m; j++)
                idx[j] = idx[j - 1] + 1;
        }
        return result;
    }

    // shareCount-1 fresh random strings, plus the message minus their sum --
    // so all shareCount shares are needed to recover msgDigits (any
    // shareCount-1 of them reveal nothing about it).
    private List<String> splitIntoShares(String msgDigits, int shareCount) {
        List<String> tbl = new ArrayList<>(shareCount);

        String tmp = msgDigits;
        for (int i = 0; i < shareCount - 1; i++) {
            String r = randDigits(msgDigits.length());
            tbl.add(r);
            tmp = stringSubtractDigits(tmp, r);
            // r and tmp are always the same length here (both
            // msgDigits.length()), so stringSubtractDigits() can't actually
            // fail on the length check.
        }
        tbl.add(tmp);
        return tbl;
    }

    // Splits plainText into maxParts numbered shares such that any minParts
    // of them reconstruct the message -- a simple additive/subtractive
    // scheme, not OTP-keyed (no key file involved). Writes
    // "<filePrefix><courier>-<group>.otp" for every courier in every
    // minParts-of-maxParts combination group.
    boolean splitMessage(String plainText, int minParts, int maxParts, String filePrefix) {
        clearError();

        if (minParts < 1 || maxParts < minParts) {
            setError("minParts/maxParts must satisfy 1 <= minParts <= maxParts");
            return false;
        }
        if (maxParts > 9) {
            // Each courier group is named by indexing into the combination's
            // concatenated-digit-string ID by character position -- that
            // only holds while every courier number is a single digit.
            // Refuse it outright past that rather than reproduce the silent
            // misbehavior otp.py/Quacque/Kwak flag for this case. See
            // kwek_design.md.
            setError("maxParts > 9 is not supported (courier numbering becomes ambiguous past single digits)");
            return false;
        }
        if (plainText.isEmpty()) {
            setError("message is empty, nothing to split");
            return false;
        }

        String encoded = encode(plainText);
        if (encoded == null)
            return false;
        String clearTextDigits = stringDigits(encoded);

        for (String messageGroup : combo(minParts, maxParts)) {
            List<String> shares = splitIntoShares(clearTextDigits, minParts);

            for (int i = 0; i < messageGroup.length(); i++) {
                String fileName = filePrefix + messageGroup.charAt(i) + "-" + messageGroup + ".otp";
                String fileData = codeGroups(shares.get(i)) + "\n";
                if (!writeFile(fileName, fileData))
                    return false;
            }
        }

        clearError();
        return true;
    }

    // Inverse of splitMessage(): reconstructs the plaintext from at least
    // minParts of the share files written above (order doesn't matter, but
    // they must all belong to the same courier group). Segment files are
    // wiped after use (unless keepKeyFilesAfterUse()). Returns
    // Optional.empty() on failure.
    Optional<String> mergeMessage(List<String> segmentFiles) {
        clearError();

        if (segmentFiles.isEmpty()) {
            setError("no segment files given");
            return Optional.empty();
        }

        String firstText = readFile(segmentFiles.get(0));
        if (firstText == null)
            return Optional.empty();

        int fileLen = stringDigits(firstText).length();
        if (fileLen < 1) {
            setError("segment file " + segmentFiles.get(0) + " contains no text");
            return Optional.empty();
        }

        // matches otp.py's/Quacque's/Kwak's behavior exactly, including
        // reading segmentFiles.get(0) again inside this loop -- redundant,
        // but this is a faithful port, not a rewrite.
        String clearTextDigits = "0".repeat(fileLen);
        for (String filename : segmentFiles) {
            String raw = readFile(filename);
            if (raw == null)
                return Optional.empty();
            String tmpText = stringDigits(raw);

            if (tmpText.length() != clearTextDigits.length()) {
                setError("message length of file " + filename + " does not match");
                return Optional.empty();
            }
            clearTextDigits = stringAddDigits(clearTextDigits, tmpText);
            if (clearTextDigits == null)
                return Optional.empty();
        }

        String plainText = decode(clearTextDigits);
        if (plainText == null)
            return Optional.empty();

        String wipeError = "";
        if (!wipeKeys(segmentFiles))
            wipeError = lastErrorMessage;
        if (!wipeError.isEmpty())
            setError(wipeError);
        else
            clearError();

        return Optional.of(plainText);
    }

    // ========================================================================
    // stream combine ("-b")
    // ========================================================================

    // Adds two equal-length digit streams together (digit-wise, mod 10, no
    // carry). combinedFile and/or keyPrefix may be empty to skip that
    // output; when keyPrefix is given, the combined stream is also sliced
    // into SHEET_SIZE-digit sheets named "<keyPrefix>-NN.otk". inputFile1/
    // inputFile2 are wiped after use (unless keepKeyFilesAfterUse()).
    boolean combineStreams(String inputFile1, String inputFile2, String combinedFile, String keyPrefix) {
        clearError();

        String rawTmp1 = readFile(inputFile1);
        if (rawTmp1 == null) return false;
        String tmp1 = stringDigits(rawTmp1);

        String rawTmp2 = readFile(inputFile2);
        if (rawTmp2 == null) return false;
        String tmp2 = stringDigits(rawTmp2);

        if (tmp1.length() != tmp2.length()) {
            setError("code stream digit counts are different");
            return false;
        }

        String tmp3 = stringAddDigits(tmp1, tmp2);
        if (tmp3 == null)
            return false;

        if (!combinedFile.isEmpty()) {
            if (!writeFile(combinedFile, codeGroups(tmp3)))
                return false;
        }

        if (!keyPrefix.isEmpty()) {
            // 0-based page numbering here, unlike keygen()'s 1-based --
            // matches otp.py's/Quacque's/Kwak's do_combineStreams() exactly.
            int digitsNeeded = tmp3.length();
            int pageCount = digitsNeeded / SHEET_SIZE;
            for (int x = 0; x < pageCount; x++) {
                String fn = keyPrefix + "-" + zeroPad(x, 2) + ".otk";
                String s = tmp3.substring(x * SHEET_SIZE, x * SHEET_SIZE + SHEET_SIZE);
                if (!writeFile(fn, codeGroups(s)))
                    return false;
            }
        }

        String wipeError = "";
        if (!wipeKeys(Arrays.asList(inputFile1, inputFile2)))
            wipeError = lastErrorMessage;
        if (!wipeError.isEmpty())
            setError(wipeError);
        else
            clearError();

        return true;
    }

    // ========================================================================
    // wipe ("-w")
    // ========================================================================

    // Securely wipes an arbitrary list of files, same overwrite-then-delete
    // logic as the internal key-wipe path. Always runs -- not gated by
    // keepKeyFilesAfterUse(), since this is an explicit user request.
    boolean wipeFiles(List<String> files) {
        clearError();
        boolean allOk = true;
        for (String fn : files) {
            if (!wipeFile(fn))
                allOk = false;
        }
        if (!allOk && lastErrorMessage.isEmpty())
            setError("one or more files failed to wipe");
        return allOk;
    }
}
