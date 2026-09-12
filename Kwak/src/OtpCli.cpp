// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#include "OtpCli.h"
#include "OTP.h"
#include "SecureRandom.h"
#include "TerminalEditor.h"
#include "Utf8.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string kEditorSentinel = "EDITOR";
const std::string kVersionStr = "v0.9.8-kwak";

// ============================================================================
// small helpers -- file<->text bridging in the CLI layer itself, exactly as
// a GUI would: OTP's public API takes std::u32string for message-shaped
// text and only deals in file paths for genuinely key-shaped (multi-file,
// persistent, removable-media) material. See OTP.h's class comment.
// ============================================================================

std::optional<std::u32string> readTextFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return utf8::decode(ss.str());
}

bool writeTextFile(const std::string &path, const std::u32string &text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    const std::string bytes = utf8::encode(text);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

// Writes content to a fresh, uniquely-named temp file under the system temp
// directory, appends its path to tempFiles (so the caller can wipe it via
// OTP::wipeFiles() once it's no longer needed), and returns the path --
// empty on failure.
std::string makeTempFile(const std::u32string &content, std::vector<std::string> &tempFiles)
{
    namespace fs = std::filesystem;

    unsigned char suffix[8];
    secure_random::fillBytes(suffix, sizeof(suffix));
    static const char hex[] = "0123456789abcdef";
    std::string name = "otp_";
    for (unsigned char b : suffix) {
        name += hex[b >> 4];
        name += hex[b & 0x0F];
    }
    name += ".otk";

    std::error_code ec;
    const fs::path path = fs::temp_directory_path(ec) / name;
    if (ec)
        return std::string();

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return std::string();
    const std::string bytes = utf8::encode(content);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    f.close();

    const std::string pathStr = path.string();
    tempFiles.push_back(pathStr);
    return pathStr;
}

// Resolves a message-shaped INPUT argument (-i for encipher, -c/-p for
// fakeMsg, -i for splitMsg): "EDITOR" opens the terminal editor and returns
// whatever was typed, staying entirely in memory, exactly like otp.py's
// EDITOR sentinel. Anything else is read from that path. std::nullopt on
// failure (unreadable file, or the editor was cancelled).
//
// Passes OTP::allowedInputChars() explicitly, exactly as otp.py's readFile()
// passes validStr.
std::optional<std::u32string> readMessageArg(const std::string &path, const std::u32string &title)
{
    if (path.empty())
        return std::nullopt;
    if (path == kEditorSentinel)
        return TerminalEditor::getText(title, OTP::allowedInputChars()); // nullopt on cancel

    const std::optional<std::u32string> text = readTextFile(path);
    if (!text) {
        std::cerr << "[FAIL] could not read file: " << path << "\n";
        return std::nullopt;
    }
    return text;
}

// Resolves a message-shaped OUTPUT argument (-o for encipher/decipher/
// mergeMsg): "EDITOR" displays the text read-only and never touches disk,
// exactly like otp.py's EDITOR sentinel. Anything else gets written there.
bool writeMessageArg(const std::string &path, const std::u32string &title, const std::u32string &text)
{
    if (path.empty())
        return false;
    if (path == kEditorSentinel) {
        TerminalEditor::showText(title, text, OTP::allowedInputChars());
        return true;
    }
    if (!writeTextFile(path, text)) {
        std::cerr << "[FAIL] could not write file: " << path << "\n";
        return false;
    }
    return true;
}

// Resolves a key-shaped INPUT argument (-i/-a for join/unjoin/combineStreams,
// -c for unjoin's combinedKeyFile): "EDITOR" opens the terminal editor and
// writes whatever was typed into a fresh temp file (tracked in tempFiles for
// wiping at the end of the run), since OTP's key-file methods need a real
// path. Anything else is returned unchanged. Empty string on cancel/failure.
//
// NOTE: unlike message-shaped EDITOR args, this briefly touches disk -- a
// securely-wiped temp file, not the "never touches the filesystem" otp.py
// promises for EDITOR. There's no way around that while OTP's key
// operations are file-based; flagged here and in kwak_design.md rather than
// glossed over.
std::string resolveKeyInputArg(const std::string &path, const std::u32string &title, std::vector<std::string> &tempFiles)
{
    if (path != kEditorSentinel)
        return path;

    const std::optional<std::u32string> typed = TerminalEditor::getText(title, OTP::allowedInputChars());
    if (!typed)
        return std::string();

    const std::string tmpPath = makeTempFile(*typed, tempFiles);
    if (tmpPath.empty())
        std::cerr << "[FAIL] could not create a temporary file for EDITOR input\n";
    return tmpPath;
}

// Resolves the trailing key-file LIST argument for encipher/decipher
// (whatever's left over in args.extra after -i/-o are consumed): each entry
// that is exactly "EDITOR" opens the terminal editor and writes what's typed
// into a fresh temp file (tracked in tempFiles for wiping later) -- a run
// mixing a real key file with a hand-typed one still gets a distinct,
// numbered title per EDITOR screen. Anything else passes through unchanged.
// Returns an empty list if any entry was cancelled -- the caller should
// treat that as fatal.
std::vector<std::string> resolveKeyListArg(const std::vector<std::string> &keyFiles, std::vector<std::string> &tempFiles)
{
    std::vector<std::string> resolved;
    const std::size_t n = keyFiles.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::string &kfn = keyFiles[i];
        if (kfn != kEditorSentinel) {
            resolved.push_back(kfn);
            continue;
        }
        const std::u32string title = (n == 1)
            ? utf8::decode("Key material")
            : utf8::decode("Key material " + std::to_string(i + 1) + " of " + std::to_string(n));
        const std::optional<std::u32string> typed = TerminalEditor::getText(title, OTP::allowedInputChars());
        if (!typed)
            return {}; // cancelled
        const std::string tmpPath = makeTempFile(*typed, tempFiles);
        if (tmpPath.empty()) {
            std::cerr << "[FAIL] could not create a temporary file for EDITOR key input\n";
            return {};
        }
        resolved.push_back(tmpPath);
    }
    return resolved;
}

// Resolves a key-shaped OUTPUT argument (-o for join's combinedKeyFile, -c
// for combineStreams' combinedFile): "EDITOR" is swapped for a temp file
// path up front (so the real OTP call has somewhere to write); after that
// call succeeds, pass the same (original, resolved) pair to
// finalizeKeyOutputArg() to display it and let the temp file get wiped with
// the rest of tempFiles.
std::string resolveKeyOutputArg(const std::string &path, std::vector<std::string> &tempFiles)
{
    if (path != kEditorSentinel)
        return path;
    return makeTempFile(std::u32string(), tempFiles);
}

void finalizeKeyOutputArg(const std::string &originalPath, const std::string &resolvedPath, const std::u32string &title)
{
    if (originalPath != kEditorSentinel || resolvedPath.empty())
        return;
    const std::optional<std::u32string> content = readTextFile(resolvedPath);
    if (content)
        TerminalEditor::showText(title, *content, OTP::allowedInputChars());
}

// ============================================================================
// version / help text -- adapted from otp.py's/Quacque's do_version()/
// do_brief_help()/do_help(), same flags and file-argument semantics.
// ============================================================================

void printVersion()
{
    std::cout << "SILENT DUCK - OTP (one time pad), Kwak standalone C++ build, version: "
              << kVersionStr
              << ",\nCopyright (c) July 1994, released under Creative Commons (CC).\n";
}

void printBriefHelp()
{
    std::cout <<
        "\n"
        "-v print out version number\n"
        "-h print help\n"
        "-z option: turn on use of Morse Shorts\n"
        "-t option: turn on testing mode to inhibit modifying the file system (no write, delete, etc.)\n"
        "-k keep input files after use to avoid auto-destruction\n"
        "-kt generate CIA TRIGON-size key sheets with -g/-gz (8 groups of 5 digits x 40 lines, 1600 digits/page) instead of the default (shorter) sheet size\n"
        "-n throttle entropy consumption sleeping for 'n' seconds after every 25 digits.\n"
        "-r set number of rounds for file wiping (writes 0, 1, random bits)\n"
        "-q specify number of extra whitening rounds folded into each key sheet during keygen\n"
        "-g generate key files\n"
        "-gz generate all-zero key files (testing only -- NEVER use for a real message)\n"
        "-e encipher file, wiping input file and keys\n"
        "-d decipher file, wiping input file and keys\n"
        "-f generate key for previously enciphered message based on known clear text\n"
        "-j use two key sheets to encipher a new random 25 sheet keypad, deleting input files\n"
        "-u use two key sheets to recover a 25 sheet keypad, deleting input files\n"
        "-s split and encipher message so we only need an arbitrary minimum to deliver it, deletes input file\n"
        "-m merge message and decipher using minimum arbitrary message segments, deleting input files\n"
        "-b combine two encoded streams together, deleting inputs after the new stream is output\n"
        "-w wipe files\n"
        "\n"
        "Use the filename \"EDITOR\" in place of any -i/-o/-c/-p/-a file argument to type/read that\n"
        "text on screen via a full-screen terminal editor instead of touching the file system. -y (a prefix that\n"
        "fans out to many files) does not support EDITOR.\n"
        "\n"
        "-hh helpful help with more text.\n"
        "\n";
}

void printFullHelp()
{
    printBriefHelp();
    std::cout <<
        "otp commands and options (Kwak standalone C++ build):\n\n"
        "# -v print out version number\n"
        "otp -v\n\n"
        "# -g generate key files -- provide filename prefix for the 25 pages, ex: keys/XX123\n"
        "otp -g -y keys/XX123\n\n"
        "# -gz generate all-zero key files -- testing only, NEVER for a real message\n"
        "otp -gz -y keys/XX123\n\n"
        "# -kt generate CIA TRIGON-size key sheets with -g/-gz: 8 groups of 5 digits\n"
        "# across, 40 lines down per page (1600 digits/page) instead of the default\n"
        "# (shorter) sheet size. The page count (25) is unchanged.\n"
        "otp -g -kt -y keys/XX123\n"
        "otp -gz -kt -y keys/XX123\n\n"
        "# -e encipher file\n"
        "otp -e -i inputfile.txt -o outputfile.otp keys/XX123-001.otk\n"
        "# type the plaintext on screen instead of reading a file:\n"
        "otp -e -i EDITOR -o outputfile.otp keys/XX123-001.otk\n\n"
        "# -d decipher file\n"
        "otp -d -i outputfile.otp -o cleartext.txt keys/XX123-001.otk\n"
        "# view the deciphered plaintext on screen instead of writing a file:\n"
        "otp -d -i outputfile.otp -o EDITOR keys/XX123-001.otk\n\n"
        "# -f generate key for a previously enciphered message based on known clear text\n"
        "otp -f -c ciphertext.otp -p plaintext.txt -y newkey.otk\n\n"
        "# -j use two key sheets to encipher a new random 25 sheet keypad\n"
        "otp -j -i key01.otk -a key02.otk -o combinedkey.otk -y keys/ZZ456\n\n"
        "# -u use two key sheets to recover a 25 sheet keypad\n"
        "otp -u -i key01.otk -a key02.otk -c combinedkey.otk -y keys/AA567\n\n"
        "# -s split and encipher message so we only need an arbitrary minimum to deliver it\n"
        "otp -s -i inputtext.txt -l 3 -x 5 splitA/AA-\n\n"
        "# -m merge message and decipher using minimum arbitrary message segments\n"
        "otp -m -o outSegmentsMsg.txt splitA/AA-123-345.otp splitA/AA-345-345.otp\n\n"
        "# -b combine two encoded streams together\n"
        "otp -b -i input1.otk -a input2.otk -c combined -y keyPrefix\n\n"
        "# -w wipe files\n"
        "otp -w keys/XX123-001.otk keys/XX123-002.otk\n";
    std::cout << OTP::referenceTablesText();
}

// ============================================================================
// argument parsing -- mirrors otp.py's process_args() table-for-table.
// ============================================================================

struct ParsedArgs
{
    std::map<std::string, bool> cmd;       // real commands -- exactly one should be true
    std::map<std::string, std::string> parm; // value-carrying flags; "" = not given
    std::vector<std::string> extra;        // positional (non-flag) arguments

    // modifiers, applied directly rather than surfaced as commands
    bool keep = false;
    bool trigon = false; // "-kt": CIA TRIGON-size key sheets with -g/-gz
    bool morseShorts = false;
    bool testing = false;
    int randomDuplicates = -1;      // -1 = not given, use OTP's default
    int wipeRoundCount = -1;
    int entropySleepSeconds = -1;
};

// Returns false (with a message on stderr) on a malformed command line.
bool parseArgs(int argc, char *argv[], ParsedArgs &out)
{
    static const std::vector<std::string> kCommandFlags = {
        "-b", "-d", "-e", "-f", "-g", "-gz", "-j", "-u", "-s", "-m", "-v", "-h", "-hh", "-w",
    };
    static const std::vector<std::string> kValueFlags = {
        "-c", "-i", "-a", "-y", "-l", "-o", "-p", "-x",
    };

    for (const std::string &c : kCommandFlags)
        out.cmd[c] = false;
    for (const std::string &v : kValueFlags)
        out.parm[v] = std::string(); // "not given"

    int x = 1;
    while (x < argc) {
        const std::string a = argv[x];

        if (a == "-k") {
            out.keep = true;
        } else if (a == "-kt") {
            out.trigon = true;
        } else if (a == "-z") {
            out.morseShorts = true;
        } else if (a == "-t") {
            out.testing = true;
        } else if (a == "-q" || a == "-r" || a == "-n") {
            ++x;
            if (x >= argc) {
                std::cerr << "[FAIL] argument \"" << a << "\" missing value\n";
                return false;
            }
            int val = 0;
            bool numOk = true;
            try {
                std::size_t consumed = 0;
                val = std::stoi(argv[x], &consumed);
                numOk = consumed == std::string(argv[x]).size();
            } catch (...) {
                numOk = false;
            }
            if (!numOk) {
                std::cerr << "[FAIL] argument \"" << a << "\" needs a numeric value\n";
                return false;
            }
            if (a == "-q") {
                if (val <= 0) { std::cerr << "[FAIL] parameter for '-q' must be greater than zero\n"; return false; }
                out.randomDuplicates = val;
            } else if (a == "-r") {
                if (val < 0) { std::cerr << "[FAIL] parameter for '-r' must not be negative\n"; return false; }
                out.wipeRoundCount = val;
            } else {
                if (val < 0) { std::cerr << "[FAIL] parameter for '-n' must not be negative\n"; return false; }
                out.entropySleepSeconds = val;
            }
        } else if (out.cmd.find(a) != out.cmd.end()) {
            out.cmd[a] = true;
        } else if (out.parm.find(a) != out.parm.end()) {
            ++x;
            if (x >= argc) {
                std::cerr << "[FAIL] argument \"" << a << "\" missing value\n";
                return false;
            }
            out.parm[a] = argv[x];
        } else {
            out.extra.push_back(a);
        }
        ++x;
    }
    return true;
}

// ============================================================================
// command implementations -- each bridges CLI arguments to one call on
// OTP's public API, then reports the result. Returns a process exit code.
// ============================================================================

int reportResult(bool ok, const std::string &lastError, const char *successMsg)
{
    if (!lastError.empty())
        std::cerr << (ok ? "[WARN] " : "[FAIL] ") << lastError << "\n";
    if (ok)
        std::cout << "[OK] " << successMsg << "\n";
    return ok ? 0 : 1;
}

int runKeygen(OTP &otp, const ParsedArgs &args, bool zeroKeys)
{
    const std::string prefix = args.parm.at("-y");
    if (prefix == kEditorSentinel) {
        std::cerr << "[FAIL] '-y EDITOR' is not supported -- keygen writes 25 separate files, "
                     "which doesn't map to a single edit buffer\n";
        return 1;
    }
    const bool ok = otp.keygen(prefix, zeroKeys, OTP::kPagesPerPad, args.trigon);
    return reportResult(ok, otp.lastError(), "key files written");
}

// otp.py's/Quacque's encipher/decipher wipe the input message *file* after
// a successful run (unless -k). Our OTP::encipher()/decipher() never see a
// file -- they take the message as in-memory text -- so that's the CLI
// layer's job, exactly as for splitMessage()'s -i in runSplitMsg() below.
void wipeSourceFileIfReal(OTP &otp, const std::string &path, const ParsedArgs &args)
{
    if (path != kEditorSentinel && !args.keep)
        otp.wipeFiles({path});
}

int runEncipher(OTP &otp, const ParsedArgs &args)
{
    const std::string iArg = args.parm.at("-i");
    const std::optional<std::u32string> plainText = readMessageArg(iArg, utf8::decode("Plaintext"));
    if (!plainText) {
        std::cerr << "[FAIL] no plaintext (missing -i, unreadable file, or EDITOR cancelled)\n";
        return 1;
    }

    std::vector<std::string> tempFiles;
    const std::vector<std::string> keyFiles = resolveKeyListArg(args.extra, tempFiles);
    if (keyFiles.empty() && !args.extra.empty()) {
        std::cerr << "[FAIL] no key material (EDITOR cancelled)\n";
        return 1;
    }

    const std::optional<std::u32string> cipherText = otp.encipher(*plainText, keyFiles);
    if (!cipherText) {
        otp.wipeFiles(tempFiles); // clean up any EDITOR-typed key temp file; encipher() failed before it could
        return reportResult(false, otp.lastError(), "");
    }
    if (!writeMessageArg(args.parm.at("-o"), utf8::decode("Ciphertext"), *cipherText))
        return 1;
    wipeSourceFileIfReal(otp, iArg, args);
    // encipher() above already wiped keyFiles (temp or real) on success --
    // see OTP::encipher()'s wipeKeys() call -- so tempFiles needs no cleanup here.
    return reportResult(true, otp.lastError(), "message enciphered");
}

int runDecipher(OTP &otp, const ParsedArgs &args)
{
    const std::string iArg = args.parm.at("-i");
    const std::optional<std::u32string> cipherText = readMessageArg(iArg, utf8::decode("Ciphertext"));
    if (!cipherText) {
        std::cerr << "[FAIL] no ciphertext (missing -i, unreadable file, or EDITOR cancelled)\n";
        return 1;
    }

    std::vector<std::string> tempFiles;
    const std::vector<std::string> keyFiles = resolveKeyListArg(args.extra, tempFiles);
    if (keyFiles.empty() && !args.extra.empty()) {
        std::cerr << "[FAIL] no key material (EDITOR cancelled)\n";
        return 1;
    }

    const std::optional<std::u32string> plainText = otp.decipher(*cipherText, keyFiles);
    if (!plainText) {
        otp.wipeFiles(tempFiles); // clean up any EDITOR-typed key temp file; decipher() failed before it could
        return reportResult(false, otp.lastError(), "");
    }
    if (!writeMessageArg(args.parm.at("-o"), utf8::decode("Plaintext"), *plainText))
        return 1;
    wipeSourceFileIfReal(otp, iArg, args);
    // decipher() above already wiped keyFiles (temp or real) on success --
    // see OTP::decipher()'s wipeKeys() call -- so tempFiles needs no cleanup here.
    return reportResult(true, otp.lastError(), "message deciphered");
}

int runFakeMsg(OTP &otp, const ParsedArgs &args)
{
    const std::optional<std::u32string> plainText = readMessageArg(args.parm.at("-p"), utf8::decode("Known plaintext"));
    const std::optional<std::u32string> cipherText = readMessageArg(args.parm.at("-c"), utf8::decode("Known ciphertext"));
    if (!plainText || !cipherText) {
        std::cerr << "[FAIL] need both -p (plaintext) and -c (ciphertext)\n";
        return 1;
    }

    std::vector<std::string> tempFiles;
    const std::string yArg = args.parm.at("-y");
    const std::string keyOut = resolveKeyOutputArg(yArg, tempFiles);
    if (keyOut.empty() && yArg == kEditorSentinel) {
        std::cerr << "[FAIL] could not create a temporary file for EDITOR output\n";
        return 1;
    }

    const bool ok = otp.generateKeyForKnownPlaintext(*plainText, *cipherText, keyOut);
    if (ok)
        finalizeKeyOutputArg(yArg, keyOut, utf8::decode("Generated key"));
    if (!tempFiles.empty())
        otp.wipeFiles(tempFiles);
    return reportResult(ok, otp.lastError(), "key generated for known plaintext/ciphertext pair");
}

int runJoinKeys(OTP &otp, const ParsedArgs &args)
{
    const std::string prefix = args.parm.at("-y");
    if (prefix == kEditorSentinel) {
        std::cerr << "[FAIL] '-y EDITOR' is not supported -- joinKeys writes 25 separate files\n";
        return 1;
    }

    std::vector<std::string> tempFiles;
    const std::string fileI = resolveKeyInputArg(args.parm.at("-i"), utf8::decode("Key sheet 1"), tempFiles);
    const std::string fileJ = resolveKeyInputArg(args.parm.at("-a"), utf8::decode("Key sheet 2"), tempFiles);
    const std::string oArg = args.parm.at("-o");
    const std::string combinedOut = resolveKeyOutputArg(oArg, tempFiles);

    bool ok = false;
    if (!fileI.empty() && !fileJ.empty() && !combinedOut.empty()) {
        ok = otp.joinKeys(fileI, fileJ, combinedOut, prefix);
        if (ok)
            finalizeKeyOutputArg(oArg, combinedOut, utf8::decode("Combined key table (send to recipient)"));
    } else {
        std::cerr << "[FAIL] missing or cancelled -i/-a/-o argument\n";
    }
    if (!tempFiles.empty())
        otp.wipeFiles(tempFiles);
    return reportResult(ok, otp.lastError(), "new keypad joined");
}

int runUnjoinKeys(OTP &otp, const ParsedArgs &args)
{
    const std::string prefix = args.parm.at("-y");
    if (prefix == kEditorSentinel) {
        std::cerr << "[FAIL] '-y EDITOR' is not supported -- unjoinKeys writes 25 separate files\n";
        return 1;
    }

    std::vector<std::string> tempFiles;
    const std::string fileI = resolveKeyInputArg(args.parm.at("-i"), utf8::decode("Key sheet 1"), tempFiles);
    const std::string fileJ = resolveKeyInputArg(args.parm.at("-a"), utf8::decode("Key sheet 2"), tempFiles);
    const std::string combinedIn = resolveKeyInputArg(args.parm.at("-c"), utf8::decode("Combined key table (received)"), tempFiles);

    bool ok = false;
    if (!fileI.empty() && !fileJ.empty() && !combinedIn.empty())
        ok = otp.unjoinKeys(fileI, fileJ, combinedIn, prefix);
    else
        std::cerr << "[FAIL] missing or cancelled -i/-a/-c argument\n";
    if (!tempFiles.empty())
        otp.wipeFiles(tempFiles);
    return reportResult(ok, otp.lastError(), "keypad recovered");
}

int runSplitMsg(OTP &otp, const ParsedArgs &args)
{
    const std::string iArg = args.parm.at("-i");
    const std::optional<std::u32string> plainText = readMessageArg(iArg, utf8::decode("Message to split"));
    if (!plainText) {
        std::cerr << "[FAIL] no message (missing -i, unreadable file, or EDITOR cancelled)\n";
        return 1;
    }
    if (args.extra.empty()) {
        std::cerr << "[FAIL] -s needs a filename prefix as a positional argument\n";
        return 1;
    }

    int minParts = 0, maxParts = 0;
    bool lOk = true, xOk = true;
    try { minParts = std::stoi(args.parm.at("-l")); } catch (...) { lOk = false; }
    try { maxParts = std::stoi(args.parm.at("-x")); } catch (...) { xOk = false; }
    if (!lOk || !xOk) {
        std::cerr << "[FAIL] -s needs -l (min parts) and -x (max parts)\n";
        return 1;
    }

    const std::string prefix = args.extra.front();
    const bool ok = otp.splitMessage(*plainText, minParts, maxParts, prefix);

    // splitMessage() only ever saw the text in memory (see its doc comment)
    // -- wipe the real source file, same as runEncipher()/runDecipher() do
    // for their -i.
    if (ok)
        wipeSourceFileIfReal(otp, iArg, args);

    return reportResult(ok, otp.lastError(), "message split");
}

int runMergeMsg(OTP &otp, const ParsedArgs &args)
{
    if (args.extra.empty()) {
        std::cerr << "[FAIL] -m needs one or more segment files as positional arguments\n";
        return 1;
    }
    const std::optional<std::u32string> plainText = otp.mergeMessage(args.extra);
    if (!plainText)
        return reportResult(false, otp.lastError(), "");
    if (!writeMessageArg(args.parm.at("-o"), utf8::decode("Merged plaintext"), *plainText))
        return 1;
    return reportResult(true, otp.lastError(), "message merged");
}

int runCombineStreams(OTP &otp, const ParsedArgs &args)
{
    const std::string keyPrefix = args.parm.at("-y");
    if (keyPrefix == kEditorSentinel) {
        std::cerr << "[FAIL] '-y EDITOR' is not supported -- combineStreams can write many sheet files\n";
        return 1;
    }

    std::vector<std::string> tempFiles;
    const std::string in1 = resolveKeyInputArg(args.parm.at("-i"), utf8::decode("Stream 1"), tempFiles);
    const std::string in2 = resolveKeyInputArg(args.parm.at("-a"), utf8::decode("Stream 2"), tempFiles);
    const std::string cArg = args.parm.at("-c");
    const std::string combinedOut = cArg.empty() ? std::string() : resolveKeyOutputArg(cArg, tempFiles);

    bool ok = false;
    if (!in1.empty() && !in2.empty()) {
        ok = otp.combineStreams(in1, in2, combinedOut, keyPrefix);
        if (ok)
            finalizeKeyOutputArg(cArg, combinedOut, utf8::decode("Combined stream"));
    } else {
        std::cerr << "[FAIL] missing or cancelled -i/-a argument\n";
    }
    if (!tempFiles.empty())
        otp.wipeFiles(tempFiles);
    return reportResult(ok, otp.lastError(), "streams combined");
}

int runWipe(OTP &otp, const ParsedArgs &args)
{
    if (args.extra.empty()) {
        std::cerr << "[FAIL] -w needs one or more files as positional arguments\n";
        return 1;
    }
    const bool ok = otp.wipeFiles(args.extra);
    return reportResult(ok, otp.lastError(), "files wiped");
}

} // namespace

int otp_main(int argc, char *argv[])
{
    if (argc == 1) {
        printBriefHelp();
        return 0;
    }

    ParsedArgs args;
    if (!parseArgs(argc, argv, args))
        return 1;

    OTP otp;
    otp.setKeepKeyFilesAfterUse(args.keep);
    otp.setUseMorseShorts(args.morseShorts);
    otp.setTestingMode(args.testing);
    if (args.wipeRoundCount >= 0)
        otp.setWipeRoundCount(args.wipeRoundCount);
    if (args.randomDuplicates >= 0)
        otp.setRandomDuplicates(args.randomDuplicates);
    if (args.entropySleepSeconds >= 0)
        otp.setEntropyGatheringSleepTime(args.entropySleepSeconds);

    // Exactly one command executes per run, matching otp.py's/Quacque's
    // otp_main() -- checked in the same order it defines cmd_args, so
    // behavior matches if more than one command flag is (accidentally)
    // given at once.
    static const std::vector<std::string> kOrder = {
        "-v", "-h", "-hh", "-d", "-e", "-f", "-g", "-gz", "-j", "-u", "-s", "-m", "-b", "-w",
    };

    for (const std::string &c : kOrder) {
        const auto it = args.cmd.find(c);
        if (it == args.cmd.end() || !it->second)
            continue;

        if (c == "-v") { printVersion(); return 0; }
        if (c == "-h") { printBriefHelp(); return 0; }
        if (c == "-hh") { printFullHelp(); return 0; }
        if (c == "-d") return runDecipher(otp, args);
        if (c == "-e") return runEncipher(otp, args);
        if (c == "-f") return runFakeMsg(otp, args);
        if (c == "-g") return runKeygen(otp, args, false);
        if (c == "-gz") return runKeygen(otp, args, true);
        if (c == "-j") return runJoinKeys(otp, args);
        if (c == "-u") return runUnjoinKeys(otp, args);
        if (c == "-s") return runSplitMsg(otp, args);
        if (c == "-m") return runMergeMsg(otp, args);
        if (c == "-b") return runCombineStreams(otp, args);
        if (c == "-w") return runWipe(otp, args);
    }

    std::cerr << "[INFO] no command given -- nothing to do. Pass -h for usage.\n";
    return 0;
}
