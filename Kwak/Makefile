# Kwak -- standalone C++ build, no Qt, no CMake. See README.md (build
# instructions) and kwak_design.md (why this exists / how it relates to
# otp.py and Quacque).
#
# Usage:
#   make                 build ./otp (otp.exe on Windows)
#   make run-selftest    build, then run the regression smoke tests
#   make clean           remove compiled objects and the executable
#   make distclean       also remove the vendored PDCursesMod checkout
#                         (Windows only -- see below)
#
# Needs only g++ (any C++17-capable g++, MinGW or otherwise) and GNU Make.
# On Linux/macOS, also needs a wide-character ncurses dev package installed
# (see README.md) -- nothing else is fetched. On Windows, the first build
# additionally needs `git` on PATH once, to fetch PDCursesMod (the "EDITOR"
# full-screen sentinel's console backend there) -- see the Windows-only
# block below; this mirrors Quacque's CMakeLists.txt FetchContent step,
# just driven by make instead of CMake.
#
# Every recipe line below is deliberately a single plain command (no shell
# loops, no brace expansion, no `&&` chains) so it runs the same whether
# make invokes cmd.exe or a POSIX sh to execute it -- both understand a bare
# `mkdir somedir` and a leading "-" to ignore a command's exit status, which
# is all these recipes need.

CXX    ?= g++
# Plain assignment, not ?= -- Make's built-in default for CC is the bare
# name "cc", which counts as "already set" so ?= would never override it,
# and plenty of toolchains (this MinGW included) don't have a "cc" on PATH
# at all, only "gcc".
CC     := gcc
CXXSTD := -std=c++17
WARN   := -Wall -Wextra
OPT    := -O2

SRC_DIR := src
OBJ_DIR := build
SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SOURCES))

ifeq ($(OS),Windows_NT)
TARGET        := otp.exe
PDCURSES_TAG  := v4.5.4
PDCURSES_DIR  := .pdcursesmod
CURSES_CFLAGS := -I$(PDCURSES_DIR) -DPDC_WIDE -DPDC_FORCE_UTF8
# -lstdc++fs: on GCC < 9 (Strawberry's bundled MinGW is 8.3.0), std::filesystem
# lives in a separate archive from the rest of libstdc++ -- otherwise ld fails
# with "undefined reference to std::filesystem::...".
CURSES_LIBS   := -lwinmm -lbcrypt -lstdc++fs
# Static-link the MinGW runtime itself (libgcc/libstdc++/libwinpthread), not
# just PDCursesMod/winmm/bcrypt. Without this, otp.exe keeps a dynamic
# dependency on libstdc++-6.dll etc., and on a machine with more than one
# MinGW install on PATH (common -- e.g. Qt bundles its own), Windows can
# resolve that dependency to a DIFFERENT, incompatible copy of the DLL at
# runtime than the one this was actually built against, e.g.:
#   "Entry Point Not Found: ...filesystem...parent_path... could not be
#   located in the dynamic link library otp.exe"
# Static linking removes the dependency entirely -- see kwak_design.md.
STATIC_FLAGS  := -static -static-libgcc -static-libstdc++
else
TARGET        := otp
CURSES_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null)
CURSES_LIBS   := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw)
THREAD_FLAGS  := -pthread
endif

CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(CURSES_CFLAGS) $(THREAD_FLAGS)
LDFLAGS  := $(STATIC_FLAGS) $(CURSES_LIBS) $(THREAD_FLAGS)

.PHONY: all build clean distclean run-selftest

# ---- POSIX (Linux/macOS): system ncursesw, nothing to fetch ----
ifneq ($(OS),Windows_NT)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)
	strip $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	-mkdir $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	-rm -f $(OBJ_DIR)/*.o $(TARGET)

distclean: clean

endif

# ---- Windows: vendor PDCursesMod once, then build ----
# Two-phase on purpose: $(wildcard $(PDCURSES_DIR)/pdcurses/*.c) below has to
# run AFTER the clone below actually happens, but Make expands $(wildcard)
# at parse time, before any recipe runs -- so `all` clones first, then
# re-invokes make in a fresh pass that sees the now-real files. (Same
# chicken-and-egg problem Quacque's CMakeLists.txt solves with
# FetchContent_Populate() running before its hand-written library target is
# declared; this is that same fix, Make-flavored.)
ifeq ($(OS),Windows_NT)

all: $(PDCURSES_DIR)/.fetched
	@$(MAKE) --no-print-directory build

build: $(TARGET)

# originally from https://github.com/Bill-Gray/PDCursesMod.git
$(PDCURSES_DIR)/.fetched:
	git clone --depth 1 --branch $(PDCURSES_TAG) https://github.com/rehartley/PDCursesMod.git $(PDCURSES_DIR)
	echo fetched > $(PDCURSES_DIR)/.fetched

PDCURSES_WINCON_SRC := $(PDCURSES_DIR)/wincon/pdcclip.c $(PDCURSES_DIR)/wincon/pdcdisp.c \
                        $(PDCURSES_DIR)/wincon/pdcgetsc.c $(PDCURSES_DIR)/wincon/pdckbd.c \
                        $(PDCURSES_DIR)/wincon/pdcscrn.c $(PDCURSES_DIR)/wincon/pdcsetsc.c \
                        $(PDCURSES_DIR)/wincon/pdcutil.c
PDCURSES_OBJ := $(patsubst $(PDCURSES_DIR)/pdcurses/%.c,$(OBJ_DIR)/pdcursesmod/pdcurses/%.o,$(wildcard $(PDCURSES_DIR)/pdcurses/*.c)) \
                $(patsubst $(PDCURSES_DIR)/wincon/%.c,$(OBJ_DIR)/pdcursesmod/wincon/%.o,$(PDCURSES_WINCON_SRC))

$(TARGET): $(OBJECTS) $(PDCURSES_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(PDCURSES_OBJ) $(LDFLAGS)
	strip $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	-mkdir $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/pdcursesmod/pdcurses/%.o: $(PDCURSES_DIR)/pdcurses/%.c
	-mkdir $(OBJ_DIR)
	-mkdir $(OBJ_DIR)/pdcursesmod
	-mkdir $(OBJ_DIR)/pdcursesmod/pdcurses
	$(CC) $(CURSES_CFLAGS) -O2 -c -o $@ $<

$(OBJ_DIR)/pdcursesmod/wincon/%.o: $(PDCURSES_DIR)/wincon/%.c
	-mkdir $(OBJ_DIR)
	-mkdir $(OBJ_DIR)/pdcursesmod
	-mkdir $(OBJ_DIR)/pdcursesmod/wincon
	$(CC) $(CURSES_CFLAGS) -O2 -c -o $@ $<

clean:
	-rd /s /q $(OBJ_DIR)
	-del /q $(TARGET)

distclean: clean
	-rd /s /q $(PDCURSES_DIR)

endif

run-selftest: all
	$(if $(filter Windows_NT,$(OS)),.\$(TARGET),./$(TARGET)) --selftest
