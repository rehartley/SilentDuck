# Kwak

A standalone, dependency-free C++17 port of [`otp.py`](../otp.py)'s OTP
(one-time pad) engine — just `g++`. Builds as **`otp`**
(`otp.exe` on Windows), a command-line tool with the same flags, on-disk
key/ciphertext formats, and behavior as the Python reference implementation.

## Prerequisites

- **A C++17 compiler** — any reasonably recent `g++` (GCC 8+; tested with
  MinGW-w64 GCC 11.2) or Clang. No library dependency beyond a curses
  implementation (below).
- **Git** — to clone this repository, and (Windows only) for the one-time
  fetch of PDCursesMod described next.
- **Windows** — the "EDITOR" full-screen sentinel needs [Bill Gray's](https://github.com/Bill-Gray/PDCursesMod)
  [PDCursesMod](https://github.com/rehartley/PDCursesMod) (pinned to
  `v4.5.4`); the Makefile/`build.bat` fetch it automatically on first build
  via a shallow `git clone` into `.pdcursesmod/` (gitignored, not vendored
  into this repo). Needs network access the first time you build.
- **Linux/macOS** — the system's real wide-character ncurses instead:
  e.g. `sudo apt install libncursesw5-dev` on Debian/Ubuntu,
  `sudo dnf install ncurses-devel` on Fedora, or `brew install ncurses` on
  macOS. Nothing is fetched. *(Flagging honestly: this path hasn't
  actually been built/tested yet — please report back if you try it.)*
- **GNU Make** — optional, only for the `make` build below. The
  `build.sh`/`build.bat` scripts need nothing but the compiler itself.

## Build

Three ways to build, same result. Pick whichever fits how you like to work.

### `make` (recommended for day-to-day development — incremental)

```bash
make              # builds ./otp (otp.exe on Windows)
make run-selftest # builds, then runs the regression smoke tests
make clean        # removes compiled objects and the executable
```

Only rebuilds what changed. On Windows, the first `make` also fetches
PDCursesMod (see above) — expect that run to take a little longer.

### One-shot script (simplest to read — one compiler invocation)

```bash
./build.sh        # Linux/macOS
```
```bat
build.bat         :: Windows
```

Each rebuilds everything from scratch with (essentially) a single compiler
command over every `.cpp` file at once — no incremental objects, nothing to
understand about Make. On Windows this is actually two commands, not one:
PDCursesMod is a plain C library (some of it isn't valid C++), so it needs
one `gcc` call of its own before the single `g++` call that builds
everything that's actually Kwak's own code — see the comment at the top of
`build.bat`.

### By hand (what the scripts above actually run)

```bash
# Linux/macOS
g++ -std=c++17 -O2 $(pkg-config --cflags ncursesw) src/*.cpp -o otp $(pkg-config --libs ncursesw) -pthread
```
```bat
:: Windows, after the one-time PDCursesMod fetch + gcc compile step in build.bat
g++ -std=c++17 -O2 -I.pdcursesmod -DPDC_WIDE -DPDC_FORCE_UTF8 src\*.cpp *.o -o otp.exe -static -static-libgcc -static-libstdc++ -lwinmm -lbcrypt
```

## Verify the build

```
./otp --selftest      # (otp.exe --selftest on Windows)
```

runs the OTP engine's regression/smoke checks used throughout development.

```
./otp -hh
```

prints full help plus the straddling-checkerboard/Morse reference tables —
a quick sanity check that the CLI itself is wired up correctly.

## Why the Windows build is statically linked

`make`, `build.bat`, and the hand-run command above all pass
`-static -static-libgcc -static-libstdc++`. Without those flags, `otp.exe`
keeps a dynamic dependency on `libstdc++-6.dll`/`libgcc_s_seh-1.dll`/
`libwinpthread-1.dll` — and on any machine with more than one MinGW install
on `PATH` (not unusual, if you have more than one IDE/toolchain installed
over time), Windows can resolve that dependency to a *different*,
incompatible copy of the DLL at runtime than the one `otp.exe` was
actually built against. The symptom is exactly this, just from
double-clicking the exe:

> Entry Point Not Found — The procedure entry point
> `_ZNKSt10filesystem7__cxx114path11parent_pathEv` could not be located in
> the dynamic link library otp.exe

Static linking removes the dependency entirely. Verified definitively, not
just "it compiled": `objdump -p otp.exe` lists only genuine Windows system
DLLs (`ADVAPI32`, `bcrypt`, `KERNEL32`, `msvcrt`, `USER32`, `WINMM`) — no
MinGW runtime DLL of any kind — and the exe runs clean with `PATH` stripped
to just `C:\Windows\System32;C:\Windows`.

(Linux/macOS builds stay dynamically linked against the system's real
ncursesw and libstdc++, which don't have this multi-toolchain collision
problem — system package management keeps exactly one of each around.)

## Repository layout

```
Kwak/
  Makefile              incremental build (recommended for development)
  build.sh / build.bat   one-shot single/two-command build scripts
  kwak_design.md          design notes -- read this next
  src/
    OTP.h / OTP.cpp          the cipher engine
    OtpCli.h / OtpCli.cpp     console CLI, otp.py-equivalent flags/behavior
    TerminalEditor.h/.cpp      curses-based full-screen "EDITOR" sentinel
                                 (same source on Windows and Linux/macOS --
                                 only the curses backend differs, chosen by
                                 the build, not the code)
    Utf8.h / Utf8.cpp          UTF-8 <-> char32_t codec, for the few places
                                 that need per-character text processing
    SecureRandom.h/.cpp         direct OS CSPRNG access for key material
    main.cpp                    entry point (otp_main(), or --selftest)
```

## Related

- [`../otp.py`](../otp.py) — the Python reference implementation; same
  flags, same on-disk formats, interoperable keys/ciphertext.
- [`../README.md`](../README.md) — top-level project README.
