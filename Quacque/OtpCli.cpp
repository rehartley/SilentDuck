#include "OtpCli.h"
#include "TerminalEditor.h"
#include "OTP.h"

#include <iostream>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextStream>

namespace {

const QString kEditorSentinel = QStringLiteral("EDITOR");
const QString kVersionStr = QStringLiteral("v0.9.8-quacque");

// ============================================================================
// small helpers -- these do file<->QString bridging in the CLI layer itself,
// exactly as a GUI would, since OTP's public API takes QString for
// message-shaped text and only deals in file paths for genuinely key-shaped
// (multi-file, persistent, removable-media) material. See OTP.h's class
// comment.
// ============================================================================

QString readTextFile(const QString &path, bool *ok)
{
    *ok = true;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *ok = false;
        return QString();
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    return in.readAll();
}

bool writeTextFile(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << text;
    return true;
}

QString makeTempFile(const QString &content, QStringList &tempFiles)
{
    QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("quacque_XXXXXX.otk")));
    tmp.setAutoRemove(false); // securely wiped via OTP::wipeFiles() at the end instead
    if (!tmp.open())
        return QString();
    tmp.write(content.toUtf8());
    tmp.close();
    const QString path = tmp.fileName();
    tempFiles.append(path);
    return path;
}

// Resolves a message-shaped INPUT argument (-i for encipher, -c/-p for
// fakeMsg, -i for splitMsg): "EDITOR" opens the terminal editor and returns
// whatever was typed, staying entirely in memory, exactly like otp.py's
// EDITOR sentinel. Anything else is read from that path. Null QString on
// failure (unreadable file, or the editor was cancelled).
QString readMessageArg(const QString &path, const QString &title)
{
    if (path.isNull())
        return QString();
    if (path == kEditorSentinel)
        return TerminalEditor::getText(title); // null on cancel

    bool ok = true;
    const QString text = readTextFile(path, &ok);
    if (!ok) {
        std::cerr << "[FAIL] could not read file: " << path.toStdString() << "\n";
        return QString();
    }
    return text;
}

// Resolves a message-shaped OUTPUT argument (-o for encipher/decipher/
// mergeMsg): "EDITOR" displays the text read-only and never touches disk,
// exactly like otp.py's EDITOR sentinel. Anything else gets written there.
bool writeMessageArg(const QString &path, const QString &title, const QString &text)
{
    if (path.isNull())
        return false;
    if (path == kEditorSentinel) {
        TerminalEditor::showText(title, text);
        return true;
    }
    if (!writeTextFile(path, text)) {
        std::cerr << "[FAIL] could not write file: " << path.toStdString() << "\n";
        return false;
    }
    return true;
}

// Resolves a key-shaped INPUT argument (-i/-a for join/unjoin/combineStreams,
// -c for unjoin's combinedKeyFile): "EDITOR" opens the terminal editor and writes
// whatever was typed into a fresh temp file (tracked in tempFiles for
// wiping at the end of the run), since OTP's key-file methods need a real
// path. Anything else is returned unchanged. Null QString on cancel/failure.
//
// NOTE: unlike message-shaped EDITOR args, this briefly touches disk -- a
// securely-wiped temp file, not the "never touches the filesystem" otp.py
// promises for EDITOR. There's no way around that while OTP's key
// operations are file-based; flagged here and in quacque_design.md rather
// than glossed over.
QString resolveKeyInputArg(const QString &path, const QString &title, QStringList &tempFiles)
{
    if (path != kEditorSentinel)
        return path;

    const QString typed = TerminalEditor::getText(title);
    if (typed.isNull())
        return QString();

    const QString tmpPath = makeTempFile(typed, tempFiles);
    if (tmpPath.isEmpty())
        std::cerr << "[FAIL] could not create a temporary file for EDITOR input\n";
    return tmpPath;
}

// Resolves a key-shaped OUTPUT argument (-o for join's combinedKeyFile, -c
// for combineStreams' combinedFile): "EDITOR" is swapped for a temp file
// path up front (so the real OTP call has somewhere to write); after that
// call succeeds, pass the same (original, resolved) pair to
// finalizeKeyOutputArg() to display it and let the temp file get wiped with
// the rest of tempFiles.
QString resolveKeyOutputArg(const QString &path, QStringList &tempFiles)
{
    if (path != kEditorSentinel)
        return path;
    return makeTempFile(QString(), tempFiles);
}

void finalizeKeyOutputArg(const QString &originalPath, const QString &resolvedPath, const QString &title)
{
    if (originalPath != kEditorSentinel || resolvedPath.isEmpty())
        return;
    bool ok = true;
    const QString content = readTextFile(resolvedPath, &ok);
    if (ok)
        TerminalEditor::showText(title, content);
}

// ============================================================================
// version / help text -- adapted from otp.py's do_version()/do_brief_help()/
// do_help(), same flags and file-argument semantics.
// ============================================================================

void printVersion()
{
    std::cout << "SILENT DUCK - OTP (one time pad), Quacque console build, version: "
              << kVersionStr.toStdString()
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
        "otp commands and options (Quacque console build):\n\n"
        "# -v print out version number\n"
        "otp -v\n\n"
        "# -g generate key files -- provide filename prefix for the 25 pages, ex: keys/XX123\n"
        "otp -g -y keys/XX123\n\n"
        "# -gz generate all-zero key files -- testing only, NEVER for a real message\n"
        "otp -gz -y keys/XX123\n\n"
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
}

// ============================================================================
// argument parsing -- mirrors otp.py's process_args() table-for-table.
// ============================================================================

struct ParsedArgs
{
    QMap<QString, bool> cmd;       // real commands -- exactly one should be true
    QMap<QString, QString> parm;   // value-carrying flags; null QString = not given
    QStringList extra;             // positional (non-flag) arguments

    // modifiers, applied directly rather than surfaced as commands --
    // mirrors process_args() intercepting -k/-z/-t/-q/-r/-n before the
    // generic cmd_args/parm_args dispatch
    bool keep = false;
    bool morseShorts = false;
    bool testing = false;
    int randomDuplicates = -1;      // -1 = not given, use OTP's default
    int wipeRoundCount = -1;
    int entropySleepSeconds = -1;
};

// Returns false (with a message on stderr) on a malformed command line.
bool parseArgs(int argc, char *argv[], ParsedArgs &out)
{
    static const QStringList kCommandFlags = {
        "-b", "-d", "-e", "-f", "-g", "-gz", "-j", "-u", "-s", "-m", "-v", "-h", "-hh", "-w",
    };
    static const QStringList kValueFlags = {
        "-c", "-i", "-a", "-y", "-l", "-o", "-p", "-x",
    };

    for (const QString &c : kCommandFlags)
        out.cmd[c] = false;
    for (const QString &v : kValueFlags)
        out.parm[v] = QString(); // null

    int x = 1;
    while (x < argc) {
        const QString a = QString::fromLocal8Bit(argv[x]);

        if (a == QStringLiteral("-k")) {
            out.keep = true;
        } else if (a == QStringLiteral("-z")) {
            out.morseShorts = true;
        } else if (a == QStringLiteral("-t")) {
            out.testing = true;
        } else if (a == QStringLiteral("-q") || a == QStringLiteral("-r") || a == QStringLiteral("-n")) {
            ++x;
            if (x >= argc) {
                std::cerr << "[FAIL] argument \"" << a.toStdString() << "\" missing value\n";
                return false;
            }
            bool numOk = false;
            const int val = QString::fromLocal8Bit(argv[x]).toInt(&numOk);
            if (!numOk) {
                std::cerr << "[FAIL] argument \"" << a.toStdString() << "\" needs a numeric value\n";
                return false;
            }
            if (a == QStringLiteral("-q")) {
                if (val <= 0) { std::cerr << "[FAIL] parameter for '-q' must be greater than zero\n"; return false; }
                out.randomDuplicates = val;
            } else if (a == QStringLiteral("-r")) {
                if (val < 0) { std::cerr << "[FAIL] parameter for '-r' must not be negative\n"; return false; }
                out.wipeRoundCount = val;
            } else {
                if (val < 0) { std::cerr << "[FAIL] parameter for '-n' must not be negative\n"; return false; }
                out.entropySleepSeconds = val;
            }
        } else if (out.cmd.contains(a)) {
            out.cmd[a] = true;
        } else if (out.parm.contains(a)) {
            ++x;
            if (x >= argc) {
                std::cerr << "[FAIL] argument \"" << a.toStdString() << "\" missing value\n";
                return false;
            }
            out.parm[a] = QString::fromLocal8Bit(argv[x]);
        } else {
            out.extra.append(a);
        }
        ++x;
    }
    return true;
}

// ============================================================================
// command implementations -- each bridges CLI arguments to one call on
// OTP's public API, then reports the result. Returns a process exit code.
// ============================================================================

int reportResult(bool ok, const QString &lastError, const char *successMsg)
{
    if (!lastError.isEmpty())
        std::cerr << (ok ? "[WARN] " : "[FAIL] ") << lastError.toStdString() << "\n";
    if (ok)
        std::cout << "[OK] " << successMsg << "\n";
    return ok ? 0 : 1;
}

int runKeygen(OTP &otp, const ParsedArgs &args, bool zeroKeys)
{
    const QString prefix = args.parm.value(QStringLiteral("-y"));
    if (prefix == kEditorSentinel) {
        std::cerr << "[FAIL] '-y EDITOR' is not supported -- keygen writes 25 separate files, "
                     "which doesn't map to a single edit buffer\n";
        return 1;
    }
    const bool ok = otp.keygen(prefix, zeroKeys);
    return reportResult(ok, otp.lastError(), "key files written");
}

// otp.py's do_encipher()/do_decipher() wipe the input message *file* after
// a successful run (unless -k). Our OTP::encipher()/decipher() never see a
// file -- they take the message as in-memory text -- so that's the CLI
// layer's job, exactly as for splitMessage()'s -i in runSplitMsg() below.
void wipeSourceFileIfReal(OTP &otp, const QString &path, const ParsedArgs &args)
{
    if (path != kEditorSentinel && !args.keep)
        otp.wipeFiles({path});
}

int runEncipher(OTP &otp, const ParsedArgs &args)
{
    const QString iArg = args.parm.value(QStringLiteral("-i"));
    const QString plainText = readMessageArg(iArg, QStringLiteral("Plaintext"));
    if (plainText.isNull()) {
        std::cerr << "[FAIL] no plaintext (missing -i, unreadable file, or EDITOR cancelled)\n";
        return 1;
    }
    const QString cipherText = otp.encipher(plainText, args.extra);
    if (cipherText.isNull())
        return reportResult(false, otp.lastError(), "");
    if (!writeMessageArg(args.parm.value(QStringLiteral("-o")), QStringLiteral("Ciphertext"), cipherText))
        return 1;
    wipeSourceFileIfReal(otp, iArg, args);
    return reportResult(true, otp.lastError(), "message enciphered");
}

int runDecipher(OTP &otp, const ParsedArgs &args)
{
    const QString iArg = args.parm.value(QStringLiteral("-i"));
    const QString cipherText = readMessageArg(iArg, QStringLiteral("Ciphertext"));
    if (cipherText.isNull()) {
        std::cerr << "[FAIL] no ciphertext (missing -i, unreadable file, or EDITOR cancelled)\n";
        return 1;
    }
    const QString plainText = otp.decipher(cipherText, args.extra);
    if (plainText.isNull())
        return reportResult(false, otp.lastError(), "");
    if (!writeMessageArg(args.parm.value(QStringLiteral("-o")), QStringLiteral("Plaintext"), plainText))
        return 1;
    wipeSourceFileIfReal(otp, iArg, args);
    return reportResult(true, otp.lastError(), "message deciphered");
}

int runFakeMsg(OTP &otp, const ParsedArgs &args)
{
    const QString plainText = readMessageArg(args.parm.value(QStringLiteral("-p")), QStringLiteral("Known plaintext"));
    const QString cipherText = readMessageArg(args.parm.value(QStringLiteral("-c")), QStringLiteral("Known ciphertext"));
    if (plainText.isNull() || cipherText.isNull()) {
        std::cerr << "[FAIL] need both -p (plaintext) and -c (ciphertext)\n";
        return 1;
    }

    QStringList tempFiles;
    const QString yArg = args.parm.value(QStringLiteral("-y"));
    const QString keyOut = resolveKeyOutputArg(yArg, tempFiles);
    if (keyOut.isEmpty() && yArg == kEditorSentinel) {
        std::cerr << "[FAIL] could not create a temporary file for EDITOR output\n";
        return 1;
    }

    const bool ok = otp.generateKeyForKnownPlaintext(plainText, cipherText, keyOut);
    if (ok)
        finalizeKeyOutputArg(yArg, keyOut, QStringLiteral("Generated key"));
    if (!tempFiles.isEmpty())
        otp.wipeFiles(tempFiles);
    return reportResult(ok, otp.lastError(), "key generated for known plaintext/ciphertext pair");
}

int runJoinKeys(OTP &otp, const ParsedArgs &args)
{
    const QString prefix = args.parm.value(QStringLiteral("-y"));
    if (prefix == kEditorSentinel) {
        std::cerr << "[FAIL] '-y EDITOR' is not supported -- joinKeys writes 25 separate files\n";
        return 1;
    }

    QStringList tempFiles;
    const QString fileI = resolveKeyInputArg(args.parm.value(QStringLiteral("-i")), QStringLiteral("Key sheet 1"), tempFiles);
    const QString fileJ = resolveKeyInputArg(args.parm.value(QStringLiteral("-a")), QStringLiteral("Key sheet 2"), tempFiles);
    const QString oArg = args.parm.value(QStringLiteral("-o"));
    const QString combinedOut = resolveKeyOutputArg(oArg, tempFiles);

    bool ok = false;
    if (!fileI.isEmpty() && !fileJ.isEmpty() && !combinedOut.isEmpty()) {
        ok = otp.joinKeys(fileI, fileJ, combinedOut, prefix);
        if (ok)
            finalizeKeyOutputArg(oArg, combinedOut, QStringLiteral("Combined key table (send to recipient)"));
    } else {
        std::cerr << "[FAIL] missing or cancelled -i/-a/-o argument\n";
    }
    if (!tempFiles.isEmpty())
        otp.wipeFiles(tempFiles);
    return reportResult(ok, otp.lastError(), "new keypad joined");
}

int runUnjoinKeys(OTP &otp, const ParsedArgs &args)
{
    const QString prefix = args.parm.value(QStringLiteral("-y"));
    if (prefix == kEditorSentinel) {
        std::cerr << "[FAIL] '-y EDITOR' is not supported -- unjoinKeys writes 25 separate files\n";
        return 1;
    }

    QStringList tempFiles;
    const QString fileI = resolveKeyInputArg(args.parm.value(QStringLiteral("-i")), QStringLiteral("Key sheet 1"), tempFiles);
    const QString fileJ = resolveKeyInputArg(args.parm.value(QStringLiteral("-a")), QStringLiteral("Key sheet 2"), tempFiles);
    const QString combinedIn = resolveKeyInputArg(args.parm.value(QStringLiteral("-c")), QStringLiteral("Combined key table (received)"), tempFiles);

    bool ok = false;
    if (!fileI.isEmpty() && !fileJ.isEmpty() && !combinedIn.isEmpty())
        ok = otp.unjoinKeys(fileI, fileJ, combinedIn, prefix);
    else
        std::cerr << "[FAIL] missing or cancelled -i/-a/-c argument\n";
    if (!tempFiles.isEmpty())
        otp.wipeFiles(tempFiles);
    return reportResult(ok, otp.lastError(), "keypad recovered");
}

int runSplitMsg(OTP &otp, const ParsedArgs &args)
{
    const QString iArg = args.parm.value(QStringLiteral("-i"));
    const QString plainText = readMessageArg(iArg, QStringLiteral("Message to split"));
    if (plainText.isNull()) {
        std::cerr << "[FAIL] no message (missing -i, unreadable file, or EDITOR cancelled)\n";
        return 1;
    }
    if (args.extra.isEmpty()) {
        std::cerr << "[FAIL] -s needs a filename prefix as a positional argument\n";
        return 1;
    }

    bool lOk = false, xOk = false;
    const int minParts = args.parm.value(QStringLiteral("-l")).toInt(&lOk);
    const int maxParts = args.parm.value(QStringLiteral("-x")).toInt(&xOk);
    if (!lOk || !xOk) {
        std::cerr << "[FAIL] -s needs -l (min parts) and -x (max parts)\n";
        return 1;
    }

    const QString prefix = args.extra.first();
    const bool ok = otp.splitMessage(plainText, minParts, maxParts, prefix);

    // splitMessage() only ever saw the text in memory (see its doc
    // comment) -- wipe the real source file, same as runEncipher()/
    // runDecipher() do for their -i.
    if (ok)
        wipeSourceFileIfReal(otp, iArg, args);

    return reportResult(ok, otp.lastError(), "message split");
}

int runMergeMsg(OTP &otp, const ParsedArgs &args)
{
    if (args.extra.isEmpty()) {
        std::cerr << "[FAIL] -m needs one or more segment files as positional arguments\n";
        return 1;
    }
    const QString plainText = otp.mergeMessage(args.extra);
    if (plainText.isNull())
        return reportResult(false, otp.lastError(), "");
    if (!writeMessageArg(args.parm.value(QStringLiteral("-o")), QStringLiteral("Merged plaintext"), plainText))
        return 1;
    return reportResult(true, otp.lastError(), "message merged");
}

int runCombineStreams(OTP &otp, const ParsedArgs &args)
{
    const QString keyPrefix = args.parm.value(QStringLiteral("-y"));
    if (keyPrefix == kEditorSentinel) {
        std::cerr << "[FAIL] '-y EDITOR' is not supported -- combineStreams can write many sheet files\n";
        return 1;
    }

    QStringList tempFiles;
    const QString in1 = resolveKeyInputArg(args.parm.value(QStringLiteral("-i")), QStringLiteral("Stream 1"), tempFiles);
    const QString in2 = resolveKeyInputArg(args.parm.value(QStringLiteral("-a")), QStringLiteral("Stream 2"), tempFiles);
    const QString cArg = args.parm.value(QStringLiteral("-c"));
    const QString combinedOut = cArg.isNull() ? QString() : resolveKeyOutputArg(cArg, tempFiles);

    bool ok = false;
    if (!in1.isEmpty() && !in2.isEmpty()) {
        ok = otp.combineStreams(in1, in2, combinedOut, keyPrefix);
        if (ok)
            finalizeKeyOutputArg(cArg, combinedOut, QStringLiteral("Combined stream"));
    } else {
        std::cerr << "[FAIL] missing or cancelled -i/-a argument\n";
    }
    if (!tempFiles.isEmpty())
        otp.wipeFiles(tempFiles);
    return reportResult(ok, otp.lastError(), "streams combined");
}

int runWipe(OTP &otp, const ParsedArgs &args)
{
    if (args.extra.isEmpty()) {
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

    // Exactly one command executes per run, matching otp.py's otp_main() --
    // checked in the same order it defines cmd_args, so behavior matches if
    // more than one command flag is (accidentally) given at once.
    static const QStringList kOrder = {
        "-v", "-h", "-hh", "-d", "-e", "-f", "-g", "-gz", "-j", "-u", "-s", "-m", "-b", "-w",
    };

    for (const QString &c : kOrder) {
        if (!args.cmd.value(c, false))
            continue;

        if (c == QStringLiteral("-v")) { printVersion(); return 0; }
        if (c == QStringLiteral("-h")) { printBriefHelp(); return 0; }
        if (c == QStringLiteral("-hh")) { printFullHelp(); return 0; }
        if (c == QStringLiteral("-d")) return runDecipher(otp, args);
        if (c == QStringLiteral("-e")) return runEncipher(otp, args);
        if (c == QStringLiteral("-f")) return runFakeMsg(otp, args);
        if (c == QStringLiteral("-g")) return runKeygen(otp, args, false);
        if (c == QStringLiteral("-gz")) return runKeygen(otp, args, true);
        if (c == QStringLiteral("-j")) return runJoinKeys(otp, args);
        if (c == QStringLiteral("-u")) return runUnjoinKeys(otp, args);
        if (c == QStringLiteral("-s")) return runSplitMsg(otp, args);
        if (c == QStringLiteral("-m")) return runMergeMsg(otp, args);
        if (c == QStringLiteral("-b")) return runCombineStreams(otp, args);
        if (c == QStringLiteral("-w")) return runWipe(otp, args);
    }

    std::cerr << "[INFO] no command given -- nothing to do. Pass -h for usage.\n";
    return 0;
}
