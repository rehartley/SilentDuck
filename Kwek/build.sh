#!/usr/bin/env bash
# Kwek build script -- mirrors Kwak's build.sh: one straightforward compiler
# invocation, no build system beyond the compiler itself (see kwek_design.md
# for why javac + shell/bat scripts over Maven/Gradle). -encoding UTF-8 is
# not optional here: the straddling checkerboard's Cyrillic table
# (OTP.java) is written as literal Cyrillic source characters, and without
# this flag javac falls back to the platform's default charset, which
# silently mis-decodes them on some systems (see kwek_design.md's "Source
# file encoding" note).
set -e
cd "$(dirname "$0")"

mkdir -p build
javac -encoding UTF-8 --release 21 -d build src/*.java

echo "Compiled classes to build/."
echo "Run it:      java -cp build Main -h"
echo "Self-test:   java -cp build Main --selftest"
