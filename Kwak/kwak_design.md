# Kwak — design notes

Kwak is a standalone C++17 port of the SilentDuck OTP engine, with **no
Qt dependency at all** — just the standard library and (for the console
"EDITOR" sentinel) a curses implementation, and no CMake — just `g++`. It
completes the lineage that started with [`otp.py`](../otp.py) (the Python
reference implementation) and continued with
[Quacque](../Quacque/quacque_design.md) (a Qt/C++ port, kept behaviorally
identical to `otp.py`, built to eventually feed a cross-platform Qt GUI).
Kwak is the third link in that chain, for anyone who wants the C++ engine
and CLI **without** taking on a Qt toolchain dependency at all.

## The big question this project started with: port `otp.py` again, or port Quacque?

**Kwak is a translation of Quacque, not a fresh port of `otp.py`.** This
was a deliberate, discussed decision, not a default:

- Quacque's cipher logic was already verified against `otp.py` line by
  line — algorithm cross-checks, a full CLI-parity pass, real build
  verification (all documented in `quacque_design.md`'s Verification
  section). Re-deriving from `otp.py` a second time would mean redoing that
  whole verification effort from scratch, with a fresh chance of new
  transcription bugs.
- Quacque already carries every fix and design decision that came out of
  the *first* port: the `otp.py` filename-inconsistency fix (see
  Quacque's "Three things found in `otp.py`" section), the fsync/wipe
  durability hardening, the `-e`/`-d` source-wipe bug caught by live
  testing, the `-kt` TRIGON sheet size, and the `-hh` reference tables.
  Porting `otp.py` fresh would mean rediscovering or reimplementing all of
  that by hand, with real risk of missing one.
- This keeps the project chain traceable (`otp.py` → Quacque → Kwak), each
  step a small, reviewable diff against the one before it, rather than
  three independent implementations all separately claiming parity with
  the Python original.
- Quacque's own design notes (the "Design fork" entry, under "Going
  further: a genuinely static build") record that dropping `QtCore`
  entirely — `QString`/`QFile`/`QRandomGenerator`/`QThread` → `std::`/
  direct OS calls — was considered and explicitly *not* chosen for Quacque
  itself, because Quacque exists specifically to feed a future Qt GUI that
  needs Qt. Kwak is that road not taken, now taken as its own project
  instead of grafted onto Quacque.
- Quacque's Verification section also mentions a "throwaway Qt-free
  `std::string`/`char32_t` C++ program" built during the original port
  just to cross-check `OTP.cpp`'s logic — evidence the `char32_t`-for-
  codepoints strategy below was already the right idea once, even before
  Kwak existed as a project.

## What's identical to Quacque, on purpose

The cipher engine's *logic* — the straddling checkerboard, digit-string
arithmetic, key join/unjoin's checksum-collision walk, message split/merge,
stream combine, wipe passes — is a near-literal transliteration of
Quacque's `OTP.cpp`, function for function, with the same control flow and
the same comments explaining *why*, not just a clean-room rewrite from the
same spec. Where Quacque's own comment says "this is exactly the kind of
code where a cleaner rewrite risks a subtle off-by-one," that reasoning
applies just as much to translating it a second time — so it wasn't
restructured here either. `OTP`'s public API also keeps the same method
names and shape (`encipher()`/`decipher()`/`keygen()`/`joinKeys()`/...) so
Quacque and Kwak stay easy to diff side by side, file for file.

## What changed: the Qt → standard-library translation

| Qt (Quacque) | Kwak | Why |
|---|---|---|
| `QString` (message-shaped text) | `std::u32string` | See "Why `char32_t`, not `wchar_t`" below. |
| `QString` (ASCII-only: digit codes, filenames) | `std::string` | No Unicode processing needed there. |
| `QChar` | `char32_t` | One codepoint, always — see below. |
| `QMap<QString, QChar>` / `QMap<QChar, QString>` | `std::map<std::string, char32_t>` / `std::map<char32_t, std::string>` | Same sorted-key iteration guarantee `OTP.cpp` relies on (e.g. the join/unjoin checksum walk). |
| `QStringList` | `std::vector<std::string>` | — |
| `QFile` / `QTextStream` (UTF-8 mode) | `std::ifstream`/`std::ofstream` (binary) + `Utf8.h`'s codec at the point text needs per-character processing | File content on disk is still plain UTF-8 bytes — unchanged from `otp.py`/Quacque. |
| `QFile`'s native handle, for `FlushFileBuffers`/`fsync` | a `FILE*` from `std::fopen`, via `_fileno`/`fileno()` | `forceFlushToDisk()` itself is untouched — it was already plain WinAPI/POSIX, not Qt, in Quacque. |
| `QRandomGenerator::system()` | `SecureRandom.h`'s `secure_random::fillBytes()`/`randDigit()` | Direct `BCryptGenRandom`/`/dev/urandom` calls — see below. |
| `QThread::sleep()` | `std::this_thread::sleep_for()` | Pure standard library, no OS branch needed at all. |
| `QTemporaryFile` / `QTemporaryDir` | small hand-rolled helpers (`makeTempFile()` in `OtpCli.cpp`, `TempDir` in `main.cpp`) using `std::filesystem` + `SecureRandom` for the unique suffix | — |
| `QString::isNull()` (the "cancelled"/"failed" signal) | `std::optional<std::u32string>` (`std::nullopt`) | Same semantics, more explicit at the type level than an empty-vs-null string distinction. |
| `QString("...").arg(n, width, 10, QChar('0'))` | a small `zeroPad(int, int)` helper | — |
| `curses.h` via vendored **PDCursesMod only** (Windows-only in Quacque) | `curses.h` via PDCursesMod on Windows, the system's real **ncursesw** on Linux/macOS | See "A genuinely portable EDITOR" below — this is a build-system choice, not a source change. |

### Why `char32_t`, not `wchar_t`

Quacque's `TerminalEditor.cpp` worked in `std::wstring`/`wchar_t`, with an
explicit comment flagging that this assumed a 16-bit `wchar_t` (true on
Windows, where it was the only target) and would need real work to port
beyond that, since `wchar_t` is 32 bits on Linux/macOS. That's exactly the
situation Kwak is now in — needing the *same source* to work correctly on
both — so this was the one deliberate improvement made while translating
rather than a straight copy: everywhere Quacque used `QChar`/`QString` for
message-shaped text, Kwak uses `char32_t`/`std::u32string` instead of
`wchar_t`/`std::wstring`. `char32_t` is a fixed 32-bit type on every
platform, so one Unicode codepoint is always exactly one array element,
full stop — no platform-conditional logic anywhere in the engine, the CLI,
or the editor. (Every character this app's checkerboard alphabet actually
uses — Roman, Cyrillic, digits, punctuation — is in the Basic Multilingual
Plane anyway, so this was never a real behavioral change, just a
correctness/portability one. See `Utf8.h`'s comment for the full reasoning,
and `TerminalEditor.cpp`'s `toWCurses()` for the one narrow point that
still has to touch real `wchar_t`, because that's curses' own wide-API
signature, not something this project controls.)

### Why direct OS CSPRNG calls, not `std::random_device`

`SecureRandom.h` calls `BCryptGenRandom` (Windows) or reads `/dev/urandom`
(POSIX) directly, rather than using `std::random_device`. The C++ standard
only requires `std::random_device` to be non-deterministic "if a
non-deterministic source is available to the implementation" — some real
standard library configurations have historically fallen back to a seeded
PRNG when no hardware source was detected. That's not a tier this project
is willing to gamble OTP key material on, so it goes straight to the OS API
instead, matching the same "no better, no worse than `otp.py`'s
`os.urandom()`" bar Quacque's own `QRandomGenerator::system()` comment
already set.

### A genuinely portable EDITOR

Both Quacque and Kwak need a real, full-screen terminal editor for the
"EDITOR" sentinel (see Quacque's `TerminalEditor.h` for why that has to be
a real terminal UI, not a GUI dialog — the reasoning carries over unchanged
here). Quacque only ever targeted Windows, so it vendors PDCursesMod
unconditionally. Kwak's actual editor *source* (`TerminalEditor.cpp`) is
identical on every platform — it's written against the plain curses API
(`initscr`, `cbreak`, `wget_wch`, `mvwaddnwstr`, ...) and doesn't know or
care which library provides that API. What differs by platform is purely a
build-time choice of *backend*:

- **Windows**: PDCursesMod (the same v4.5.4, same wincon backend Quacque
  uses), fetched via a `git clone` the Makefile/`build.bat` drive directly
  (mirroring Quacque's CMake `FetchContent` step, just without CMake).
- **Linux/macOS**: the system's own installed wide-character ncurses
  (`ncursesw`), via `pkg-config` where available. Nothing to fetch.

So "portable between Windows and Linux" here means exactly what it sounds
like: no `#ifdef` anywhere in `TerminalEditor.cpp` for which curses library
is underneath it, ever — only the build system's linker/include flags
change per platform.

## Real problems hit while doing this, worth recording

**1. `.c` files compiled with `g++` instead of `gcc` broke on real code.**
PDCursesMod is plain C. The first build attempt compiled it with `$(CXX)`
(g++) since that's simplest in a Makefile — and failed with `invalid
conversion from 'void*' to 'RIPPEDOFFLINE*'` in `kernel.c`, then (once a
`-fpermissive` workaround was tried for that) a cascade of `'min' was
not declared in this scope` errors in `overlay.c`/`pad.c`/`touch.c`. C
allows an implicit `void*` conversion and apparently expects `min`/`max`
to come from somewhere g++'s C++ mode doesn't expose the same way; neither
error means the *code* is wrong, just that compiling C as C++ isn't a safe
default. Fixed by giving the Makefile/`build.bat` a real `CC` (`gcc`) for
PDCursesMod's sources and keeping `g++`/`$(CXX)` only for this project's
own `.cpp` files.

**2. `CC ?= gcc` in the Makefile silently did nothing.** GNU Make has a
*built-in* default value for `CC` (the bare name `cc`), which counts as
"already set" — so `?=` (only-if-unset) never actually overrides it, and
this MinGW toolchain has no `cc.exe` at all, only `gcc.exe`. The build
failed with `CreateProcess ... cc ... failed: The system cannot find the
file specified` until this became a plain `CC := gcc` instead.

**3. `$(wildcard ...)` on a directory that doesn't exist yet returns
nothing — a classic Make chicken-and-egg problem.** The Windows build needs
to `git clone` PDCursesMod, then compile every `.c` file in it — but Make
expands `$(wildcard $(PDCURSES_DIR)/pdcurses/*.c)` once, at parse time,
before any recipe (including the clone) has run, so the object list came
back empty on a first build. Fixed the same way Quacque's own
`CMakeLists.txt` note describes solving the analogous problem with
`FetchContent_Populate()`: the `all` target clones first, then re-invokes
`$(MAKE)` in a fresh pass, and *that* pass's `$(wildcard)` correctly sees
the now-real files.

**4. Recipe lines have to work under either `cmd.exe` or a POSIX `sh`,
because which one GNU Make uses on Windows depends on what's on `PATH`, not
on the OS.** A bare Windows/MinGW install (this project's actual target)
has no `sh.exe`, so Make falls back to `cmd.exe` — but a dev machine with
Git Bash/MSYS installed puts a real `sh.exe` on `PATH`, which Make prefers
instead. Recipes written in one shell's syntax (`if not exist ... mkdir
...`, or a POSIX `for f in ...; do ... done` loop) silently break under the
other. Fixed by keeping every recipe line to the intersection of both --
a bare `mkdir somedir`, prefixed with `-` so Make ignores the "already
exists" error regardless of which shell's `mkdir` produced it -- and
avoiding shell loops entirely (the two-phase recursive-`make` fix in \#3
above already gives every compiled file its own trivial one-line recipe,
so no loop was needed there anyway).

**5. The port's own smoke test had a stale filename check, caught by
actually running it.** `main.cpp`'s join/unjoin regression test (carried
over from Quacque's) checked for `<prefix>NN.otk` (join, no dash, 2-digit)
versus `<prefix>-NN.otk` (unjoin, dashed, 2-digit) — the *old*,
pre-fix naming scheme from before Quacque's own "inconsistent `.otk`
filename schemes" fix (see `quacque_design.md`'s "Three things found in
`otp.py`" \#1). `joinKeys()`/`unjoinKeys()` themselves were already fixed,
in both Quacque and this port, to both write `<prefix>-NNN.otk` (dashed,
3-digit) — but the *test* checking their output was never updated to
match, in either codebase. Running `--selftest` for real (not just trusting
a straight port of test code that "used to pass") caught this immediately;
fixed by updating the test's expected filenames to match what the
functions actually write now.

**6. The built `otp.exe` needed static linking for the exact same reason
Quacque's did.** A dynamically-linked build kept a dependency on
`libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll` — and this
machine (like the one Quacque was built on) has more than one MinGW
install, so double-clicking the exe (rather than running it from a shell
with the right `bin` directory on `PATH`) resolved that dependency to an
incompatible copy of the DLL and failed with `Entry Point Not Found:
...filesystem...parent_path...`. Fixed identically to Quacque's own "making
the built exe actually runnable" fix: `-static -static-libgcc
-static-libstdc++` on the final link step. Verified the same way too --
`objdump -p otp.exe` lists only genuine Windows system DLLs (`ADVAPI32`,
`bcrypt`, `KERNEL32`, `msvcrt`, `USER32`, `WINMM`), and the exe runs clean
with `PATH` stripped to just `C:\Windows\System32;C:\Windows`. Unlike
Quacque, this didn't need a whole separate statically-built Qt (the biggest
part of Quacque's own static-build effort) — there's no Qt here to begin
with, so this was just the ordinary MinGW static-runtime flags, no
multi-hour toolchain build required.

## Verification done so far

**1. `--selftest` (`OTP`-level regression checks)**, the same five checks
Quacque's smoke test runs: keygen→encipher→decipher round trip,
joinKeys/unjoinKeys deriving an identical 25-sheet keypad, splitMessage→
mergeMessage (2-of-4 courier shares), `generateKeyForKnownPlaintext`'s
repudiation property, and combineStreams+wipeFiles. All pass.

**2. Direct interop with `otp.py`, not just with itself.** A mixed
Cyrillic/Roman/digit message ("Тест Cross Implementation 007") was
enciphered with `otp.exe -e`, and the resulting ciphertext deciphered
correctly by `otp.py -d` using a copy of the same key — confirming the
on-disk ciphertext format, the straddling checkerboard, and the digit-shift
codes are genuinely bit-for-bit compatible across implementations, not just
internally self-consistent.

**3. `-kt` (TRIGON sheets) and `-z` (Morse cut shorts), exercised live**,
not just at the `OTP` API level: `otp -g -kt` produces the expected
1600-digit sheet size, and a message enciphered and deciphered with `-z` on
round-trips correctly through the Morse-cut substitution.

**4. `-hh`'s reference tables, diffed against `otp.py`'s and Quacque's
output.** Byte-for-byte identical across all three implementations (Roman
and Cyrillic digit-code↔letter tables, both Morse cut shorts tables, and
the Morse-to-cut comparison table) — see the git history for the `-hh`
reference-tables feature that was added to all three around the same time.

**5. The static Windows build, verified definitively, not just "it
compiled"** — see problem \#6 above.

## Open questions / next steps

- **Linux/macOS hasn't actually been built yet** — the `ncursesw`
  detection and `TerminalEditor.cpp` compiling against a real ncursesw (not
  just PDCursesMod) is untested. Flagging honestly, same as Quacque's own
  never-tested non-Windows `find_package(Curses)` path.
- **Manually exercise `TerminalEditor` (the "EDITOR" sentinel) in a real
  terminal** — same open item Quacque still has: typing, save (F2/Ctrl+S),
  cancel (ESC), scrolling a long buffer, and the read-only display mode all
  need a human at a real keyboard, which no amount of automated testing
  substitutes for.
- **PDCursesMod's own license** should be double-checked before any real
  distribution of a build that vendors it (see its own `LICENSE` file in
  the fetched checkout) — not evaluated in depth here, same as it wasn't
  re-litigated for Quacque, which already made this same choice of library.
- Decide whether it's worth periodically diffing `OTP.cpp` against
  Quacque's to catch the two drifting apart, given they're meant to stay
  behaviorally identical but are now two separately-edited files.
