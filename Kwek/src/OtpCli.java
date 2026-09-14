// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

// OtpCli -- console entry point logic, functionally equivalent to Kwak's
// OtpCli.h/.cpp (and, through it, Quacque's and otp.py's): same flags, same
// file-argument semantics. Built entirely on OTP's public API. See
// ../kwek_design.md.
//
// ONE DELIBERATE GAP from Kwak: the "EDITOR" filename sentinel (type/view
// message- or key-shaped text via a full-screen terminal editor instead of
// touching the filesystem) is not implemented in this build. Kwak's
// TerminalEditor hasn't been translated yet (the planned target is
// Lanterna -- see kwek_design.md's "What's not here yet"). Every call site
// below that would have opened the editor now fails cleanly instead,
// through the same "missing/unreadable/cancelled" error paths Kwak already
// has for a genuinely cancelled EDITOR screen -- so passing "EDITOR" today
// behaves like Ctrl+C'ing out of it, not like a crash or silent
// misbehavior. -y (a prefix that fans out to many files) never supported
// EDITOR in any port, including this one, for the same reason it never did:
// keygen/joinKeys/unjoinKeys/combineStreams-with-a-prefix write many files,
// which doesn't map to a single edit buffer.
final class OtpCli {
    private OtpCli() { }

    private static final String EDITOR_SENTINEL = "EDITOR";
    private static final String VERSION_STR = "v0.1.0-kwek";

    private static boolean isEditor(String path) {
        return EDITOR_SENTINEL.equals(path);
    }

    // ========================================================================
    // small helpers -- file<->text bridging in the CLI layer itself, exactly
    // as Kwak's OtpCli.cpp does it: OTP's public API takes a plain String
    // for message-shaped text and only deals in file paths for genuinely
    // key-shaped (multi-file, persistent, removable-media) material. See
    // OTP.java's class comment.
    // ========================================================================

    private static String readTextFile(String path) {
        try {
            return new String(Files.readAllBytes(Paths.get(path)), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return null;
        }
    }

    private static boolean writeTextFile(String path, String text) {
        try {
            Files.write(Paths.get(path), text.getBytes(StandardCharsets.UTF_8));
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    // Writes content to a fresh, securely-unique temp file under the system
    // temp directory, appends its path to tempFiles (so the caller can wipe
    // it via OTP.wipeFiles() once it's no longer needed), and returns the
    // path -- null on failure. Simpler than Kwak's makeTempFile(): Java's
    // Files.createTempFile() already gives a securely-random unique name in
    // the system temp directory in one call, so there's no hand-rolled
    // random-hex-suffix loop to write here (Kwak needs one because
    // std::filesystem has no built-in "give me a unique temp path" call).
    private static String makeTempFile(String content, List<String> tempFiles) {
        try {
            Path p = Files.createTempFile("otp_", ".otk");
            Files.write(p, content.getBytes(StandardCharsets.UTF_8));
            String pathStr = p.toString();
            tempFiles.add(pathStr);
            return pathStr;
        } catch (IOException e) {
            return null;
        }
    }

    // Resolves a message-shaped INPUT argument (-i for encipher, -c/-p for
    // fakeMsg, -i for splitMsg). Anything other than "EDITOR" is read from
    // that path; "EDITOR" fails here (see class comment) rather than opening
    // an editor. Returns null on failure (unreadable file, EDITOR, or an
    // empty/missing path) -- the caller reports why.
    private static String readMessageArg(String path, String title) {
        if (path == null || path.isEmpty() || isEditor(path))
            return null;
        String text = readTextFile(path);
        if (text == null)
            System.err.println("[FAIL] could not read file: " + path);
        return text;
    }

    // Resolves a message-shaped OUTPUT argument (-o for encipher/decipher/
    // mergeMsg). "EDITOR" fails here (see class comment) rather than
    // displaying the text read-only; anything else gets written there.
    private static boolean writeMessageArg(String path, String title, String text) {
        if (path == null || path.isEmpty() || isEditor(path))
            return false;
        if (!writeTextFile(path, text)) {
            System.err.println("[FAIL] could not write file: " + path);
            return false;
        }
        return true;
    }

    // Resolves a key-shaped INPUT argument (-i/-a for join/unjoin/
    // combineStreams, -c for unjoin's combinedKeyFile). "EDITOR" fails here
    // (see class comment); anything else is returned unchanged. Empty string
    // signals failure, same as a cancelled Kwak EDITOR screen would.
    private static String resolveKeyInputArg(String path, String title, List<String> tempFiles) {
        if (isEditor(path))
            return "";
        return path;
    }

    // Resolves the trailing key-file LIST argument for encipher/decipher
    // (whatever's left over in args.extra after -i/-o are consumed). Any
    // entry that is exactly "EDITOR" fails here (see class comment) rather
    // than opening an editor per entry. Returns an empty list if any entry
    // was "EDITOR" -- the caller should treat that as fatal, same as a
    // cancelled Kwak EDITOR screen.
    private static List<String> resolveKeyListArg(List<String> keyFiles, List<String> tempFiles) {
        List<String> resolved = new ArrayList<>();
        for (String kfn : keyFiles) {
            if (isEditor(kfn))
                return Collections.emptyList();
            resolved.add(kfn);
        }
        return resolved;
    }

    // Resolves a key-shaped OUTPUT argument (-o for join's combinedKeyFile,
    // -c for combineStreams' combinedFile). "EDITOR" fails here (see class
    // comment) instead of being swapped for a temp file to display later.
    private static String resolveKeyOutputArg(String path, List<String> tempFiles) {
        if (isEditor(path))
            return "";
        return path;
    }

    // Companion to resolveKeyOutputArg(): when a real OTP call wrote to a
    // temp file standing in for an "EDITOR" -o/-c, this would display that
    // content and let the temp file get wiped with the rest of tempFiles.
    // Unreachable in this build -- resolveKeyOutputArg() above already fails
    // before any OTP call runs when originalPath is "EDITOR" -- kept only so
    // OtpCli.java's call sites stay structurally parallel to Kwak's
    // OtpCli.cpp, as the natural hookup point once TerminalEditor exists.
    private static void finalizeKeyOutputArg(String originalPath, String resolvedPath, String title) {
        if (!isEditor(originalPath) || resolvedPath == null || resolvedPath.isEmpty())
            return;
        String content = readTextFile(resolvedPath);
        if (content != null) {
            // TerminalEditor.showText(title, content, OTP.allowedInputChars()) -- not yet implemented.
        }
    }

    // ========================================================================
    // version / help text -- adapted from otp.py's/Quacque's/Kwak's
    // do_version()/do_brief_help()/do_help(), same flags and file-argument
    // semantics (EDITOR examples removed/annotated -- see class comment).
    // ========================================================================

    private static void printVersion() {
        System.out.println("SILENT DUCK - OTP (one time pad), Kwek Java build, version: "
                + VERSION_STR
                + ",\nCopyright (c) July 1994, released under Creative Commons (CC).");
    }

    private static void printBriefHelp() {
        System.out.print(
            "\n" +
            "-v print out version number\n" +
            "-h print help\n" +
            "-z option: turn on use of Morse Shorts\n" +
            "-t option: turn on testing mode to inhibit modifying the file system (no write, delete, etc.)\n" +
            "-k keep input files after use to avoid auto-destruction\n" +
            "-kt generate CIA TRIGON-size key sheets with -g/-gz (8 groups of 5 digits x 40 lines, 1600 digits/page) instead of the default (shorter) sheet size\n" +
            "-n throttle entropy consumption sleeping for 'n' seconds after every 25 digits.\n" +
            "-r set number of rounds for file wiping (writes 0, 1, random bits)\n" +
            "-q specify number of extra whitening rounds folded into each key sheet during keygen\n" +
            "-g generate key files\n" +
            "-gz generate all-zero key files (testing only -- NEVER use for a real message)\n" +
            "-e encipher file, wiping input file and keys\n" +
            "-d decipher file, wiping input file and keys\n" +
            "-f generate key for previously enciphered message based on known clear text\n" +
            "-j use two key sheets to encipher a new random 25 sheet keypad, deleting input files\n" +
            "-u use two key sheets to recover a 25 sheet keypad, deleting input files\n" +
            "-s split and encipher message so we only need an arbitrary minimum to deliver it, deletes input file\n" +
            "-m merge message and decipher using minimum arbitrary message segments, deleting input files\n" +
            "-b combine two encoded streams together, deleting inputs after the new stream is output\n" +
            "-w wipe files\n" +
            "\n" +
            "otp.py/Quacque/Kwak support the filename \"EDITOR\" in place of any -i/-o/-c/-p/-a file\n" +
            "argument, to type/read that text via a full-screen terminal editor instead of touching\n" +
            "the file system. This Kwek build does not implement EDITOR yet (see kwek_design.md) --\n" +
            "pass a real file path for every -i/-o/-c/-p/-a argument. -y (a prefix that fans out to\n" +
            "many files) never supported EDITOR in any port, including this one.\n" +
            "\n" +
            "-hh helpful help with more text.\n" +
            "\n"
        );
    }

    private static void printFullHelp() {
        printBriefHelp();
        System.out.print(
            "otp commands and options (Kwek Java build):\n\n" +
            "# -v print out version number\n" +
            "otp -v\n\n" +
            "# -g generate key files -- provide filename prefix for the 25 pages, ex: keys/XX123\n" +
            "otp -g -y keys/XX123\n\n" +
            "# -gz generate all-zero key files -- testing only, NEVER for a real message\n" +
            "otp -gz -y keys/XX123\n\n" +
            "# -kt generate CIA TRIGON-size key sheets with -g/-gz: 8 groups of 5 digits\n" +
            "# across, 40 lines down per page (1600 digits/page) instead of the default\n" +
            "# (shorter) sheet size. The page count (25) is unchanged.\n" +
            "otp -g -kt -y keys/XX123\n" +
            "otp -gz -kt -y keys/XX123\n\n" +
            "# -e encipher file\n" +
            "otp -e -i inputfile.txt -o outputfile.otp keys/XX123-001.otk\n\n" +
            "# -d decipher file\n" +
            "otp -d -i outputfile.otp -o cleartext.txt keys/XX123-001.otk\n\n" +
            "# -f generate key for a previously enciphered message based on known clear text\n" +
            "otp -f -c ciphertext.otp -p plaintext.txt -y newkey.otk\n\n" +
            "# -j use two key sheets to encipher a new random 25 sheet keypad\n" +
            "otp -j -i key01.otk -a key02.otk -o combinedkey.otk -y keys/ZZ456\n\n" +
            "# -u use two key sheets to recover a 25 sheet keypad\n" +
            "otp -u -i key01.otk -a key02.otk -c combinedkey.otk -y keys/AA567\n\n" +
            "# -s split and encipher message so we only need an arbitrary minimum to deliver it\n" +
            "otp -s -i inputtext.txt -l 3 -x 5 splitA/AA-\n\n" +
            "# -m merge message and decipher using minimum arbitrary message segments\n" +
            "otp -m -o outSegmentsMsg.txt splitA/AA-123-345.otp splitA/AA-345-345.otp\n\n" +
            "# -b combine two encoded streams together\n" +
            "otp -b -i input1.otk -a input2.otk -c combined -y keyPrefix\n\n" +
            "# -w wipe files\n" +
            "otp -w keys/XX123-001.otk keys/XX123-002.otk\n"
        );
        System.out.print(OTP.referenceTablesText());
    }

    // ========================================================================
    // argument parsing -- mirrors otp.py's/Kwak's process_args()/parseArgs()
    // table-for-table.
    // ========================================================================

    private static final class ParsedArgs {
        final Map<String, Boolean> cmd = new LinkedHashMap<>();   // real commands -- exactly one should be true
        final Map<String, String> parm = new LinkedHashMap<>();   // value-carrying flags; "" = not given
        final List<String> extra = new ArrayList<>();             // positional (non-flag) arguments

        // modifiers, applied directly rather than surfaced as commands
        boolean keep = false;
        boolean trigon = false; // "-kt": CIA TRIGON-size key sheets with -g/-gz
        boolean morseShorts = false;
        boolean testing = false;
        int randomDuplicates = -1;   // -1 = not given, use OTP's default
        int wipeRoundCount = -1;
        int entropySleepSeconds = -1;
    }

    private static final List<String> COMMAND_FLAGS = List.of(
        "-b", "-d", "-e", "-f", "-g", "-gz", "-j", "-u", "-s", "-m", "-v", "-h", "-hh", "-w");
    private static final List<String> VALUE_FLAGS = List.of(
        "-c", "-i", "-a", "-y", "-l", "-o", "-p", "-x");

    // Returns null (with a message on stderr) on a malformed command line.
    private static ParsedArgs parseArgs(String[] argv) {
        ParsedArgs out = new ParsedArgs();
        for (String c : COMMAND_FLAGS) out.cmd.put(c, false);
        for (String v : VALUE_FLAGS) out.parm.put(v, "");

        int x = 0;
        while (x < argv.length) {
            String a = argv[x];

            if (a.equals("-k")) {
                out.keep = true;
            } else if (a.equals("-kt")) {
                out.trigon = true;
            } else if (a.equals("-z")) {
                out.morseShorts = true;
            } else if (a.equals("-t")) {
                out.testing = true;
            } else if (a.equals("-q") || a.equals("-r") || a.equals("-n")) {
                x++;
                if (x >= argv.length) {
                    System.err.println("[FAIL] argument \"" + a + "\" missing value");
                    return null;
                }
                int val;
                try {
                    val = Integer.parseInt(argv[x]);
                } catch (NumberFormatException e) {
                    System.err.println("[FAIL] argument \"" + a + "\" needs a numeric value");
                    return null;
                }
                if (a.equals("-q")) {
                    if (val <= 0) { System.err.println("[FAIL] parameter for '-q' must be greater than zero"); return null; }
                    out.randomDuplicates = val;
                } else if (a.equals("-r")) {
                    if (val < 0) { System.err.println("[FAIL] parameter for '-r' must not be negative"); return null; }
                    out.wipeRoundCount = val;
                } else {
                    if (val < 0) { System.err.println("[FAIL] parameter for '-n' must not be negative"); return null; }
                    out.entropySleepSeconds = val;
                }
            } else if (out.cmd.containsKey(a)) {
                out.cmd.put(a, true);
            } else if (out.parm.containsKey(a)) {
                x++;
                if (x >= argv.length) {
                    System.err.println("[FAIL] argument \"" + a + "\" missing value");
                    return null;
                }
                out.parm.put(a, argv[x]);
            } else {
                out.extra.add(a);
            }
            x++;
        }
        return out;
    }

    // ========================================================================
    // command implementations -- each bridges CLI arguments to one call on
    // OTP's public API, then reports the result. Returns a process exit code.
    // ========================================================================

    private static int reportResult(boolean ok, String lastError, String successMsg) {
        if (!lastError.isEmpty())
            System.err.println((ok ? "[WARN] " : "[FAIL] ") + lastError);
        if (ok)
            System.out.println("[OK] " + successMsg);
        return ok ? 0 : 1;
    }

    private static int runKeygen(OTP otp, ParsedArgs args, boolean zeroKeys) {
        String prefix = args.parm.get("-y");
        if (isEditor(prefix)) {
            System.err.println("[FAIL] '-y EDITOR' is not supported -- keygen writes 25 separate files, "
                    + "which doesn't map to a single edit buffer");
            return 1;
        }
        boolean ok = otp.keygen(prefix, zeroKeys, OTP.PAGES_PER_PAD, args.trigon);
        return reportResult(ok, otp.lastError(), "key files written");
    }

    // otp.py's/Kwak's encipher/decipher wipe the input message *file* after
    // a successful run (unless -k). Our OTP.encipher()/decipher() never see
    // a file -- they take the message as in-memory text -- so that's the CLI
    // layer's job, exactly as for splitMessage()'s -i in runSplitMsg() below.
    private static void wipeSourceFileIfReal(OTP otp, String path, ParsedArgs args) {
        if (!isEditor(path) && !args.keep)
            otp.wipeFiles(List.of(path));
    }

    private static int runEncipher(OTP otp, ParsedArgs args) {
        String iArg = args.parm.get("-i");
        String plainText = readMessageArg(iArg, "Plaintext");
        if (plainText == null) {
            System.err.println("[FAIL] no plaintext (missing -i, unreadable file, or 'EDITOR' is not supported in this build yet)");
            return 1;
        }

        List<String> tempFiles = new ArrayList<>();
        List<String> keyFiles = resolveKeyListArg(args.extra, tempFiles);
        if (keyFiles.isEmpty() && !args.extra.isEmpty()) {
            System.err.println("[FAIL] no key material ('EDITOR' is not supported in this build yet)");
            return 1;
        }

        Optional<String> cipherText = otp.encipher(plainText, keyFiles);
        if (cipherText.isEmpty()) {
            otp.wipeFiles(tempFiles); // clean up any temp files; encipher() failed before it could
            return reportResult(false, otp.lastError(), "");
        }
        if (!writeMessageArg(args.parm.get("-o"), "Ciphertext", cipherText.get()))
            return 1;
        wipeSourceFileIfReal(otp, iArg, args);
        // encipher() above already wiped keyFiles (real files) on success --
        // see OTP.encipher()'s wipeKeys() call -- so tempFiles needs no
        // cleanup here.
        return reportResult(true, otp.lastError(), "message enciphered");
    }

    private static int runDecipher(OTP otp, ParsedArgs args) {
        String iArg = args.parm.get("-i");
        String cipherText = readMessageArg(iArg, "Ciphertext");
        if (cipherText == null) {
            System.err.println("[FAIL] no ciphertext (missing -i, unreadable file, or 'EDITOR' is not supported in this build yet)");
            return 1;
        }

        List<String> tempFiles = new ArrayList<>();
        List<String> keyFiles = resolveKeyListArg(args.extra, tempFiles);
        if (keyFiles.isEmpty() && !args.extra.isEmpty()) {
            System.err.println("[FAIL] no key material ('EDITOR' is not supported in this build yet)");
            return 1;
        }

        Optional<String> plainText = otp.decipher(cipherText, keyFiles);
        if (plainText.isEmpty()) {
            otp.wipeFiles(tempFiles);
            return reportResult(false, otp.lastError(), "");
        }
        if (!writeMessageArg(args.parm.get("-o"), "Plaintext", plainText.get()))
            return 1;
        wipeSourceFileIfReal(otp, iArg, args);
        return reportResult(true, otp.lastError(), "message deciphered");
    }

    private static int runFakeMsg(OTP otp, ParsedArgs args) {
        String plainText = readMessageArg(args.parm.get("-p"), "Known plaintext");
        String cipherText = readMessageArg(args.parm.get("-c"), "Known ciphertext");
        if (plainText == null || cipherText == null) {
            System.err.println("[FAIL] need both -p (plaintext) and -c (ciphertext)");
            return 1;
        }

        List<String> tempFiles = new ArrayList<>();
        String yArg = args.parm.get("-y");
        String keyOut = resolveKeyOutputArg(yArg, tempFiles);
        if (keyOut.isEmpty() && isEditor(yArg)) {
            System.err.println("[FAIL] 'EDITOR' is not supported in this build yet");
            return 1;
        }

        boolean ok = otp.generateKeyForKnownPlaintext(plainText, cipherText, keyOut);
        if (ok)
            finalizeKeyOutputArg(yArg, keyOut, "Generated key");
        if (!tempFiles.isEmpty())
            otp.wipeFiles(tempFiles);
        return reportResult(ok, otp.lastError(), "key generated for known plaintext/ciphertext pair");
    }

    private static int runJoinKeys(OTP otp, ParsedArgs args) {
        String prefix = args.parm.get("-y");
        if (isEditor(prefix)) {
            System.err.println("[FAIL] '-y EDITOR' is not supported -- joinKeys writes 25 separate files");
            return 1;
        }

        List<String> tempFiles = new ArrayList<>();
        String fileI = resolveKeyInputArg(args.parm.get("-i"), "Key sheet 1", tempFiles);
        String fileJ = resolveKeyInputArg(args.parm.get("-a"), "Key sheet 2", tempFiles);
        String oArg = args.parm.get("-o");
        String combinedOut = resolveKeyOutputArg(oArg, tempFiles);

        boolean ok = false;
        if (!fileI.isEmpty() && !fileJ.isEmpty() && !combinedOut.isEmpty()) {
            ok = otp.joinKeys(fileI, fileJ, combinedOut, prefix);
            if (ok)
                finalizeKeyOutputArg(oArg, combinedOut, "Combined key table (send to recipient)");
        } else {
            System.err.println("[FAIL] missing -i/-a/-o argument, or 'EDITOR' is not supported in this build yet");
        }
        if (!tempFiles.isEmpty())
            otp.wipeFiles(tempFiles);
        return reportResult(ok, otp.lastError(), "new keypad joined");
    }

    private static int runUnjoinKeys(OTP otp, ParsedArgs args) {
        String prefix = args.parm.get("-y");
        if (isEditor(prefix)) {
            System.err.println("[FAIL] '-y EDITOR' is not supported -- unjoinKeys writes 25 separate files");
            return 1;
        }

        List<String> tempFiles = new ArrayList<>();
        String fileI = resolveKeyInputArg(args.parm.get("-i"), "Key sheet 1", tempFiles);
        String fileJ = resolveKeyInputArg(args.parm.get("-a"), "Key sheet 2", tempFiles);
        String combinedIn = resolveKeyInputArg(args.parm.get("-c"), "Combined key table (received)", tempFiles);

        boolean ok = false;
        if (!fileI.isEmpty() && !fileJ.isEmpty() && !combinedIn.isEmpty())
            ok = otp.unjoinKeys(fileI, fileJ, combinedIn, prefix);
        else
            System.err.println("[FAIL] missing -i/-a/-c argument, or 'EDITOR' is not supported in this build yet");
        if (!tempFiles.isEmpty())
            otp.wipeFiles(tempFiles);
        return reportResult(ok, otp.lastError(), "keypad recovered");
    }

    private static int runSplitMsg(OTP otp, ParsedArgs args) {
        String iArg = args.parm.get("-i");
        String plainText = readMessageArg(iArg, "Message to split");
        if (plainText == null) {
            System.err.println("[FAIL] no message (missing -i, unreadable file, or 'EDITOR' is not supported in this build yet)");
            return 1;
        }
        if (args.extra.isEmpty()) {
            System.err.println("[FAIL] -s needs a filename prefix as a positional argument");
            return 1;
        }

        int minParts;
        int maxParts;
        try {
            minParts = Integer.parseInt(args.parm.get("-l"));
            maxParts = Integer.parseInt(args.parm.get("-x"));
        } catch (NumberFormatException e) {
            System.err.println("[FAIL] -s needs -l (min parts) and -x (max parts)");
            return 1;
        }

        String prefix = args.extra.get(0);
        boolean ok = otp.splitMessage(plainText, minParts, maxParts, prefix);

        // splitMessage() only ever saw the text in memory (see its doc
        // comment) -- wipe the real source file, same as runEncipher()/
        // runDecipher() do for their -i.
        if (ok)
            wipeSourceFileIfReal(otp, iArg, args);

        return reportResult(ok, otp.lastError(), "message split");
    }

    private static int runMergeMsg(OTP otp, ParsedArgs args) {
        if (args.extra.isEmpty()) {
            System.err.println("[FAIL] -m needs one or more segment files as positional arguments");
            return 1;
        }
        Optional<String> plainText = otp.mergeMessage(args.extra);
        if (plainText.isEmpty())
            return reportResult(false, otp.lastError(), "");
        if (!writeMessageArg(args.parm.get("-o"), "Merged plaintext", plainText.get()))
            return 1;
        return reportResult(true, otp.lastError(), "message merged");
    }

    private static int runCombineStreams(OTP otp, ParsedArgs args) {
        String keyPrefix = args.parm.get("-y");
        if (isEditor(keyPrefix)) {
            System.err.println("[FAIL] '-y EDITOR' is not supported -- combineStreams can write many sheet files");
            return 1;
        }

        List<String> tempFiles = new ArrayList<>();
        String in1 = resolveKeyInputArg(args.parm.get("-i"), "Stream 1", tempFiles);
        String in2 = resolveKeyInputArg(args.parm.get("-a"), "Stream 2", tempFiles);
        String cArg = args.parm.get("-c");
        String combinedOut = cArg.isEmpty() ? "" : resolveKeyOutputArg(cArg, tempFiles);

        boolean ok = false;
        if (!in1.isEmpty() && !in2.isEmpty()) {
            ok = otp.combineStreams(in1, in2, combinedOut, keyPrefix);
            if (ok)
                finalizeKeyOutputArg(cArg, combinedOut, "Combined stream");
        } else {
            System.err.println("[FAIL] missing -i/-a argument, or 'EDITOR' is not supported in this build yet");
        }
        if (!tempFiles.isEmpty())
            otp.wipeFiles(tempFiles);
        return reportResult(ok, otp.lastError(), "streams combined");
    }

    private static int runWipe(OTP otp, ParsedArgs args) {
        if (args.extra.isEmpty()) {
            System.err.println("[FAIL] -w needs one or more files as positional arguments");
            return 1;
        }
        boolean ok = otp.wipeFiles(args.extra);
        return reportResult(ok, otp.lastError(), "files wiped");
    }

    // ========================================================================
    // entry point -- mirrors Kwak's otp_main(argc, argv).
    // ========================================================================

    private static final List<String> COMMAND_ORDER = List.of(
        "-v", "-h", "-hh", "-d", "-e", "-f", "-g", "-gz", "-j", "-u", "-s", "-m", "-b", "-w");

    static int run(String[] args) {
        if (args.length == 0) {
            printBriefHelp();
            return 0;
        }

        ParsedArgs parsed = parseArgs(args);
        if (parsed == null)
            return 1;

        OTP otp = new OTP();
        otp.setKeepKeyFilesAfterUse(parsed.keep);
        otp.setUseMorseShorts(parsed.morseShorts);
        otp.setTestingMode(parsed.testing);
        if (parsed.wipeRoundCount >= 0)
            otp.setWipeRoundCount(parsed.wipeRoundCount);
        if (parsed.randomDuplicates >= 0)
            otp.setRandomDuplicates(parsed.randomDuplicates);
        if (parsed.entropySleepSeconds >= 0)
            otp.setEntropyGatheringSleepTime(parsed.entropySleepSeconds);

        // Exactly one command executes per run, matching otp.py's/Kwak's
        // otp_main() -- checked in the same order it defines cmd_args, so
        // behavior matches if more than one command flag is (accidentally)
        // given at once.
        for (String c : COMMAND_ORDER) {
            Boolean set = parsed.cmd.get(c);
            if (set == null || !set)
                continue;

            switch (c) {
                case "-v": printVersion(); return 0;
                case "-h": printBriefHelp(); return 0;
                case "-hh": printFullHelp(); return 0;
                case "-d": return runDecipher(otp, parsed);
                case "-e": return runEncipher(otp, parsed);
                case "-f": return runFakeMsg(otp, parsed);
                case "-g": return runKeygen(otp, parsed, false);
                case "-gz": return runKeygen(otp, parsed, true);
                case "-j": return runJoinKeys(otp, parsed);
                case "-u": return runUnjoinKeys(otp, parsed);
                case "-s": return runSplitMsg(otp, parsed);
                case "-m": return runMergeMsg(otp, parsed);
                case "-b": return runCombineStreams(otp, parsed);
                case "-w": return runWipe(otp, parsed);
                default: break;
            }
        }

        System.err.println("[INFO] no command given -- nothing to do. Pass -h for usage.");
        return 0;
    }
}
