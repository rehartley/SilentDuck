# Kwek — design notes

Kwek is a Java 21 port of the SilentDuck OTP engine, built with nothing but
`javac` — no Maven, no Gradle. It is the fourth link in the lineage that
started with [`otp.py`](../otp.py) (the Python reference implementation),
continued with [Quacque](../Quacque/quacque_design.md) (a Qt/C++ port, kept
behaviorally identical to `otp.py`, built to eventually feed a cross-platform
Qt GUI), and then [Kwak](../Kwak/kwak_design.md) (a standalone C++17 port
with no Qt dependency at all, translated from Quacque rather than from
`otp.py` directly). Kwek exists for anyone who wants the engine and CLI on
the JVM, with no C++ toolchain at all.

## The question this project started with: port `otp.py`/Quacque again, or port Kwak?

**Kwek is a translation of Kwak, not a fresh port of `otp.py` or Quacque.**
Same reasoning Kwak itself gave for translating from Quacque instead of
`otp.py` (see `kwak_design.md`'s "The big question this project started
with"), carried one link further down the chain:

- Kwak's cipher logic already carries every fix and design decision that
  came out of *both* earlier ports — the `otp.py` filename-inconsistency
  fix, the fsync/wipe durability hardening, the `-e`/`-d` source-wipe fix,
  the `-kt` TRIGON sheet size, the `-hh` reference tables — verified against
  `otp.py` and cross-checked against Quacque along the way (see
  `kwak_design.md`'s Verification section). Re-deriving from `otp.py` or
  Quacque a third time would mean redoing all of that discovery from
  scratch, with a fresh chance of new transcription bugs.
- This keeps the project chain traceable (`otp.py` → Quacque → Kwak →
  Kwek), each step a small, reviewable diff against the one before it,
  rather than four independent implementations all separately claiming
  parity with the Python original.
- Kwak is *already* Qt-free, standard-library-only C++ (that was the whole
  point of Kwak existing as its own project rather than being grafted onto
  Quacque). Java has no Qt bindings to reckon with either, so Kwak's
  `std::ifstream`/direct-OS-CSPRNG/`std::u32string`-shaped design maps far
  more directly onto `java.nio.file`/`java.security.SecureRandom`/a plain
  Java `String` than Quacque's `QFile`/`QRandomGenerator`/`QString` would
  have. Translating from Kwak means translating from the port that already
  made the "no framework" decision, not undoing one.

## What's identical to Kwak, on purpose

The cipher engine's *logic* — the straddling checkerboard, digit-string
arithmetic, key join/unjoin's checksum-collision walk, message split/merge,
stream combine, wipe passes — is a near-literal transliteration of Kwak's
`OTP.cpp`, method for method, with the same control flow and the same
comments explaining *why*, not a clean-room rewrite from the same spec.
Where Kwak's own comment says "this is exactly the kind of code where a
cleaner rewrite risks a subtle off-by-one" (about `decode()`'s index
arithmetic), that reasoning applies just as much to translating it a third
time — so it wasn't restructured here either. `OTP`'s public API also keeps
the same method names and shape (`encipher()`/`decipher()`/`keygen()`/
`joinKeys()`/...) so Kwak and Kwek stay easy to diff side by side, file for
file.

## What changed: the C++ → Java translation

| C++ (Kwak) | Java (Kwek) | Why |
|---|---|---|
| `std::u32string` (message-shaped text) | `String` | See "Why a plain `String` works here, no `Utf8`-equivalent class" below. |
| `std::string` (ASCII-only: digit codes, filenames) | `String` | Java has one string type; no separate byte-string layer needed. |
| `char32_t` | `char` | One codepoint, always, for this alphabet — see below. |
| `std::map<std::string, char32_t>` / `std::map<char32_t, std::string>` | `TreeMap<String, Character>` / `TreeMap<Character, String>` | Same sorted-key iteration guarantee `OTP.java` relies on (e.g. the join/unjoin checksum walk, and `referenceTablesText()`'s formatting). |
| `std::vector<std::string>` | `List<String>` | — |
| `std::ifstream`/`std::ofstream` (binary) + `Utf8.h`'s codec | `Files.readAllBytes()`/`FileOutputStream` + `StandardCharsets.UTF_8` | File content on disk is still plain UTF-8 bytes — unchanged from `otp.py`/Quacque/Kwak. No separate codec class needed; see below. |
| `forceFlushToDisk()` (`#ifdef`'d `FlushFileBuffers`/`fsync`) | `FileOutputStream.getFD().sync()` / `RandomAccessFile.getFD().sync()` | One portable call replaces the platform conditional entirely — `FileDescriptor.sync()` *is* `FlushFileBuffers`/`fsync` under the hood on every JVM platform. |
| `SecureRandom.h`'s `secure_random::fillBytes()`/`randDigit()` | `Csprng.fillBytes()`/`Csprng.randDigit()` (wraps `java.security.SecureRandom`) | Same rejection-sampling digit primitive, same security tier — see "Why `Csprng`, not `SecureRandom`" below. |
| `std::this_thread::sleep_for()` | `Thread.sleep()` | Direct equivalent; wrapped in try/catch to restore the interrupt flag, since Java's checked `InterruptedException` has no C++ analogue. |
| `std::optional<std::u32string>` (`std::nullopt`) | `Optional<String>` (`Optional.empty()`) | Same semantics, same idiom on this platform. |
| `bool *ok` out-parameter on private helpers | private helpers return `null` on failure instead | No C++-style out-parameter idiom in Java; callers check for `null` exactly where Kwak's callers check `*ok`, and `lastError()` is already set either way. |
| `QString("...").arg(n, width, 10, QChar('0'))`-descended `zeroPad(int, int)` helper | same `zeroPad(int, int)` helper | Carried straight across, unchanged. |
| Kwak's `readTextFile()`/`writeTextFile()` (CLI-layer UTF-8 bridging) | same, via `Files.readAllBytes()`/`Files.write()` + `StandardCharsets.UTF_8` | Direct equivalent, no codec class needed — see "Why a plain `String` works here" above. |
| Kwak's `makeTempFile()` (hand-rolled random-hex-suffix unique name) | `Files.createTempFile("otp_", ".otk")` | One standard-library call already gives a securely-unique temp path; no hand-rolled suffix generator needed. |
| Kwak's `TempDir` (`main.cpp`'s RAII `--selftest` scratch directory) | `Files.createTempDirectory()` + an `AutoCloseable` wrapper, used in try-with-resources | Same "one call, no hand-rolled unique name" simplification as `makeTempFile()` above. |
| `SetConsoleOutputCP(CP_UTF8)` (Windows-only, `main.cpp`) | `System.setOut()`/`System.setErr()` wrapped in a `PrintStream` forced to `StandardCharsets.UTF_8` | Only the *write* side of the same problem — see "Console output encoding" below for the gap this leaves. |
| Kwak's `TerminalEditor` (curses-based "EDITOR" sentinel) | **not yet ported** | Deferred to a later pass — see "What's not here yet" below. |

### Why a plain `String` works here, no `Utf8`-equivalent class

Kwak needs its own `Utf8.h`/`.cpp` because C++ doesn't hand you a
platform-portable "one codepoint" type for free: `wchar_t` is 16 bits on
Windows and 32 bits on Linux/macOS, so Kwak deliberately uses `char32_t`
instead (see `Utf8.h`'s own comment, and `kwak_design.md`'s "Why `char32_t`,
not `wchar_t`" section, for the full reasoning — this was itself the one
deliberate change Kwak made translating from Quacque's `wchar_t`-based
`TerminalEditor.cpp`).

Java's `char` is *always* a 16-bit UTF-16 code unit, on every platform —
there is no Windows-vs-Linux split to route around in the first place. And
per that same `Utf8.h` comment, already true for two ports running: every
character this app's straddling checkerboard actually uses (Roman and
Cyrillic letters, digits, punctuation) is in the Basic Multilingual Plane,
i.e. never a surrogate pair. So "one `char32_t` per character" in Kwak and
"one Java `char` per character" here are exactly equivalent for this
alphabet, with no portability gap to design around and no codec class
needed at all: `OTP.java` operates on plain `String`/`StringBuilder`
throughout, and the only place UTF-8 bytes-vs-codepoints matters is at the
literal disk boundary (`readFile()`/`writeFile()`), handled with
`StandardCharsets.UTF_8` inline rather than through a separate `Utf8`
class. This is a genuine simplification specific to Java, not corner-cutting
— it's the same "check whether this port's own platform already solves the
portability problem the earlier one had to solve by hand" question Kwak
asked of `std::random_device` (see below) and of `char32_t` itself, applied
one link further down the chain.

### Why `Csprng`, not a class named `SecureRandom`

Named `Csprng`, not `SecureRandom`, purely to avoid shadowing
`java.security.SecureRandom` in any file that imports it — same security
tier that name implies, not a different one. `java.security.SecureRandom`'s
no-arg constructor is itself backed directly by the platform's CSPRNG
(Windows-PRNG/CryptGenRandom-family on Windows, NativePRNG over
`getrandom(2)`/`/dev/urandom` on Linux, etc.) via the JCA's default
provider. That means Kwek doesn't need the "go around the standard library,
straight to the OS API" step Kwak's `SecureRandom.h` documents needing (see
`kwak_design.md`'s "Why direct OS CSPRNG calls, not `std::random_device`"):
C++'s `std::random_device` carries no non-determinism guarantee at all if no
hardware source is available, but the JCA's `SecureRandom` contract does not
have that same escape hatch — Java specifies it must be cryptographically
strong. `Csprng.randDigit()` still reimplements the same by-hand
rejection-sampling primitive Kwak uses (reject any byte ≥ 250 before
`% 10`), rather than trusting `SecureRandom.nextInt(10)`'s own unbiasedness
claim, so this one four-line primitive stays the same auditable shape in
every port — `otp.py`, Quacque, Kwak, and now Kwek.

## What's not here yet

The cipher engine (`OTP.java` + `Csprng.java`) and the CLI/console entry
point (`OtpCli.java` + `Main.java`) are both ported and tested (see
Verification below) — every `otp.py`/Kwak flag works from a real command
line. One piece of Kwak remains deliberately unported:

- **`TerminalEditor.h`/`.cpp` (the curses-based full-screen "EDITOR"
  sentinel)** — deferred by explicit decision, not oversight. Kwak's own
  curses backend is itself platform-conditional at build time (PDCursesMod
  on Windows, the system's `ncursesw` on Linux/macOS, same *source* either
  way — see `kwak_design.md`'s "A genuinely portable EDITOR"). The Java
  equivalent will use [Lanterna](https://github.com/mabe02/lanterna) (a pure-
  Java curses-like TUI library, no native dependency, works from one jar on
  every platform) rather than reaching for a native curses binding — decided
  but not yet built.

  In the meantime, `OtpCli.java` recognizes the `"EDITOR"` filename sentinel
  at every call site Kwak does, and fails there cleanly and explicitly
  (`[FAIL] ... 'EDITOR' is not supported in this build yet`, exit code 1)
  rather than silently doing something else — the same code path a
  genuinely *cancelled* Kwak EDITOR screen already takes, just always taken.
  So a script or a habit built against `otp.py`/Kwak's `EDITOR` convention
  fails loudly on Kwek today instead of misbehaving quietly. `-y` (a prefix
  that fans out to many files) never supported `EDITOR` in any port,
  including this one, for the structural reason given in `OtpCli.java`'s own
  top comment.

### Console output encoding

Kwak's `main.cpp` calls `SetConsoleOutputCP(CP_UTF8)` on Windows before
anything else, because every string it prints (message content, error text,
`-hh`'s Cyrillic reference tables) is UTF-8, and a legacy-codepage Windows
console otherwise renders that as mojibake. `Main.java` replicates the
*write* half of that fix — wrapping `System.out`/`System.err` in a
`PrintStream` forced to `StandardCharsets.UTF_8` — but not the *read* half:
there's no portable way to call the Windows API that changes the console's
own codepage without a native binding (JNI/JNA), which this project isn't
willing to take on just for that (see the "just `javac`" build philosophy
in the top-level README entry for Kwek). Practical effect: a fresh Windows
console may need `chcp 65001` run once (or Windows' "Use Unicode UTF-8 for
worldwide language support" system setting turned on) before Cyrillic text
in `-hh`'s tables or a deciphered message displays correctly here. Verified
this isn't just theoretical — the Cyrillic tables render correctly under
Git Bash's `mintty` (which already defaults to UTF-8) but this hasn't yet
been checked against a stock `cmd.exe`/`PowerShell` console with the legacy
codepage active.

## Source file encoding — a real problem hit while doing this, worth recording

`OTP.java`'s Cyrillic straddling-checkerboard table (`buildNumber2Cyr()`) is
written as literal Cyrillic source characters (`'А'`, `'Е'`, ... — matching
Java source conventions, where `char32_t` numeric escapes aren't needed the
way Kwak's `OTP.cpp` needs them for the reason its own top comment gives:
C++ has no standard notion of "this source file is UTF-8"). The first smoke
test run produced `?` for every Cyrillic character, on both the input
literal *and* the round-tripped output — not a cipher bug, but `javac`
silently decoding the `.java` source file using the platform's default
charset (not UTF-8) when no encoding is specified, mis-reading the very
literals the checkerboard table is built from before the cipher logic ever
runs. Fixed by always compiling with `-encoding UTF-8` explicitly (both
`build.sh`/`build.bat` pass it) — never relying on the platform default.
This is the Java-specific analogue of Kwak's own numeric-escape workaround:
different mechanism (a compiler flag instead of `\uXXXX`-only literals), same
underlying hazard (source-file encoding assumptions silently corrupting the
one part of this program where that corruption would be hardest to notice).

## Verification done so far

**1. `--selftest` (`OTP`-level regression checks)**, folded into `Main.java`
the same five checks Kwak's own `--selftest` runs (see `main.cpp`): keygen→
encipher→decipher round trip, `joinKeys`/`unjoinKeys` deriving an identical
25-sheet keypad, `splitMessage`→`mergeMessage` (2-of-4 courier shares),
`generateKeyForKnownPlaintext`'s repudiation property, and `combineStreams`+
`wipeFiles`. All pass.

**2. The CLI itself, exercised live, not just at the `OTP` API level:**
`-v` (version banner), `-g -y <prefix>` (writes all 25 `.otk` sheets), a
real `-e`/`-d` round trip through actual files with `-k` (message enciphered
to a `.otp` file, deciphered back byte-for-byte to the original plaintext),
`-hh` (the Cyrillic reference tables render correctly under a UTF-8
terminal — see "Console output encoding" above for the one platform gap
this doesn't close), passing `EDITOR` to `-i` (fails cleanly with the
expected message and exit code 1, rather than hanging or misbehaving), and
running with no arguments (prints brief help, exits 0). All behave as
designed.

**3. Earlier ad hoc round-trip smoke test** (predates `--selftest` existing;
superseded by \#1 above but records one thing \#1 doesn't): a mixed
Roman/Cyrillic/digit message round-tripped correctly through both an
all-zero pad and a real random-generated key, confirming automatic
Latin↔Cyrillic alphabet-switching works and that literal `~`/`#` control
characters are consumed rather than echoed back, matching every earlier
port's behavior.

**4. Not yet done:**
- Direct on-disk interop with `otp.py`/Quacque/Kwak — encipher with one
  implementation, decipher with another, the way `kwak_design.md`'s
  Verification section \#2 confirms for Kwak against `otp.py`. Should be
  done before Kwek is trusted for anything real; nothing in the translation
  above should break it (same UTF-8-on-disk format, same digit-code
  tables), but "should" isn't "verified."
- `-kt` (TRIGON sheets) and `-z` (Morse cut shorts) — implemented (see
  `OTP.java`'s `keygen(..., trigon)` and `toMorseCut()`/`fromMorseCut()`,
  and `OtpCli.java`'s `-kt`/`-z` flag wiring) but not yet exercised live.
- `-hh`'s reference tables (`OTP.referenceTablesText()`) — rendered
  correctly (see \#2 above), but not yet diffed byte-for-byte against
  `otp.py`'s/Quacque's/Kwak's output the way `kwak_design.md`'s
  Verification \#4 records doing for Kwak.
- `-j`/`-u`, `-f`, `-s`/`-m`, `-b`, `-w` through the actual CLI (as opposed
  to `OTP`'s API directly, which \#1 above covers for all of them) — not
  yet exercised live end-to-end.
- Console encoding under a stock Windows `cmd.exe`/PowerShell terminal with
  the legacy codepage active — see "Console output encoding" above.

## Open questions / next steps

- Decide packaging: a runnable `kwek.jar` via `jar cfe`, built by the same
  `build.sh`/`build.bat` — no separate build tool, matching Kwak's "just the
  compiler" minimalism. Not done yet; `java -cp build Main` works fine for
  now.
- Port `TerminalEditor` on top of Lanterna, then wire it back into
  `OtpCli.java`'s `EDITOR`-sentinel call sites (each already has a comment
  marking it as the hookup point).
- Work through the "Not yet done" list above, especially the direct
  interop check against `otp.py`/Kwak — the one item that actually matters
  before trusting Kwek with anything real.
- Decide whether it's worth periodically diffing `OTP.java`/`OtpCli.java`
  against Kwak's `OTP.cpp`/`OtpCli.cpp` to catch the two drifting apart,
  given they're meant to stay behaviorally identical but are now two
  separately-edited codebases — same open item `kwak_design.md` records for
  itself against Quacque.
