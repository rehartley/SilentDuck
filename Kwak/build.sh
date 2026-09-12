#!/bin/sh
# One-shot build for Linux/macOS: a single g++ invocation compiling every
# source file at once and linking straight to the final binary -- no
# Makefile, no incremental objects, just "g++ ... -o otp". Rebuilds
# everything from scratch every time; for incremental builds during
# day-to-day development, use `make` instead (see README.md). Needs a
# wide-character ncurses dev package installed (see README.md) but fetches
# nothing else.
set -e

CURSES_CFLAGS="$(pkg-config --cflags ncursesw 2>/dev/null || true)"
CURSES_LIBS="$(pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw)"

g++ -std=c++17 -Wall -Wextra -O2 $CURSES_CFLAGS \
    src/*.cpp \
    -o otp \
    $CURSES_LIBS -pthread

echo "Built: ./otp"
