# Kwek

A Java 21 port of [`otp.py`](../otp.py)'s OTP (one-time pad) engine —
translated from [Kwak](../Kwak/kwak_design.md)'s C++17 port, not from
`otp.py` directly. Built with just `javac`, no Maven/Gradle. See
[kwek_design.md](kwek_design.md) for the full design rationale, the
Kwak → Java translation table, and why this exists as its own project.

**Status: the cipher engine and the CLI are ported and tested; the
full-screen "EDITOR" sentinel is not implemented yet.** Every `otp.py`/
Kwak flag works from a real command line and file paths — passing the
filename `EDITOR` to any `-i`/`-o`/`-c`/`-p`/`-a` argument fails cleanly
with an explanatory message instead of opening an editor. See
`kwek_design.md`'s "What's not here yet" section.

## Prerequisites

- **JDK 21** (or later) — `javac`/`java` on `PATH`. No other dependency.

## Build

```bash
./build.sh        # Linux/macOS
```
```bat
build.bat         :: Windows
```

Each compiles every `.java` file in `src/` into `build/` with
`javac -encoding UTF-8 --release 21`. The `-encoding UTF-8` flag is not
optional — see [kwek_design.md](kwek_design.md)'s "Source file encoding"
note for why. There's no `make`/incremental option yet; a javac invocation
over a handful of files is fast enough on its own that one wasn't needed.

## Run it

```bash
java -cp build Main -h              # help
java -cp build Main -g -y keys/XX   # generate a 25-page keypad: keys/XX-001.otk ... XX-025.otk
java -cp build Main -e -i in.txt -o out.otp keys/XX-001.otk   # encipher
java -cp build Main -d -i out.otp -o out.txt keys/XX-001.otk  # decipher
java -cp build Main --selftest      # regression checks, in a throwaway temp dir
```

Same flags, same on-disk `.otk`/ciphertext formats as `otp.py`/Kwak — see
[Usage.md](../Usage.md) for the full option table (every flag below `-w` in
`otp -hh` maps directly; `EDITOR` is the one exception, not yet supported
here).

## Repository layout

```
Kwek/
  build.sh / build.bat   one-shot build scripts (javac only, no build tool)
  kwek_design.md          design notes -- read this next
  src/
    OTP.java                 the cipher engine (translated from Kwak's OTP.h/.cpp)
    Csprng.java               direct OS CSPRNG access for key material
                                (translated from Kwak's SecureRandom.h/.cpp)
    OtpCli.java               console CLI, otp.py/Kwak-equivalent flags/behavior
                                (translated from Kwak's OtpCli.h/.cpp)
    Main.java                 entry point (OtpCli.run(), or --selftest)
                                (translated from Kwak's main.cpp)
```

A Lanterna-based `TerminalEditor.java` (the "EDITOR" sentinel) is planned
but not yet written — see `kwek_design.md`.

## Related

- [`../otp.py`](../otp.py) — the Python reference implementation.
- [`../Quacque`](../Quacque/quacque_design.md) — the Qt/C++ port.
- [`../Kwak`](../Kwak/kwak_design.md) — the standalone C++17 port Kwek was
  translated from.
- [`../README.md`](../README.md) — top-level project README.
- [kwek_design.md](kwek_design.md) — full design rationale, the translation
  table, and open questions.
