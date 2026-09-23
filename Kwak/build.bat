@echo off
rem One-shot build for Windows. The actual compile+link of this project's
rem own code is a single g++ invocation (the second command below, using a
rem wildcard for every .cpp at once) -- no CMake, no Makefile, just g++.
rem
rem The one thing that can't be folded into that same g++ command: the
rem vendored PDCursesMod library (the "EDITOR" full-screen sentinel's
rem Windows console backend) is plain C, and some of it (min()/max(),
rem implicit void*-to-typed-pointer conversions) simply isn't valid C++ --
rem compiling it with g++ instead of gcc fails outright. So this is a real
rem two-command build: one gcc call for PDCursesMod's C sources, one g++
rem call for everything that's actually Kwak's own code. Rebuilds
rem everything from scratch every time; for incremental rebuilds during
rem day-to-day development, use `make` instead (see README.md).
setlocal

if not exist .pdcursesmod (
    git clone --depth 1 --branch v4.5.4 https://github.com/Bill-Gray/PDCursesMod.git .pdcursesmod
    if errorlevel 1 exit /b 1
)

gcc -I.pdcursesmod -DPDC_WIDE -DPDC_FORCE_UTF8 -O2 -c .pdcursesmod\pdcurses\*.c .pdcursesmod\wincon\pdcclip.c .pdcursesmod\wincon\pdcdisp.c .pdcursesmod\wincon\pdcgetsc.c .pdcursesmod\wincon\pdckbd.c .pdcursesmod\wincon\pdcscrn.c .pdcursesmod\wincon\pdcsetsc.c .pdcursesmod\wincon\pdcutil.c
if errorlevel 1 exit /b 1

rem -static/-static-libgcc/-static-libstdc++: without these, otp.exe keeps
rem a dynamic dependency on libstdc++-6.dll etc., and on a machine with more
rem than one MinGW install on PATH (common -- e.g. Qt bundles its own),
rem Windows can resolve that dependency to a DIFFERENT, incompatible copy of
rem the DLL at runtime than the one this was actually built against --
rem symptom: "Entry Point Not Found" when just double-clicking the exe.
rem Static linking removes the dependency entirely -- see kwak_design.md.
rem
rem -lstdc++fs: on GCC < 9 (Strawberry's bundled MinGW is 8.3.0),
rem std::filesystem lives in a separate archive from the rest of libstdc++ --
rem otherwise ld fails with "undefined reference to std::filesystem::...".
g++ -std=c++17 -Wall -Wextra -O2 -I.pdcursesmod -DPDC_WIDE -DPDC_FORCE_UTF8 src\*.cpp *.o -o otp.exe -static -static-libgcc -static-libstdc++ -lwinmm -lbcrypt -lstdc++fs
if errorlevel 1 exit /b 1

rem Strip debug symbols now that the build has succeeded -- static linking
rem (above) pulls the whole MinGW runtime into otp.exe, and unstripped that
rem carries a lot of symbol/debug-info weight for a binary nobody steps a
rem debugger through in the field.
strip otp.exe

del *.o >nul 2>&1

echo Built: otp.exe
