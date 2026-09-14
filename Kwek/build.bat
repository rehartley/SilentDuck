@echo off
setlocal
rem Kwek build script -- mirrors Kwak's build.bat: one straightforward
rem compiler invocation, no build system beyond the compiler itself (see
rem kwek_design.md for why javac + shell/bat scripts over Maven/Gradle).
rem -encoding UTF-8 is not optional here: the straddling checkerboard's
rem Cyrillic table (OTP.java) is written as literal Cyrillic source
rem characters, and without this flag javac falls back to the platform's
rem default charset, which silently mis-decodes them on some systems (see
rem kwek_design.md's "Source file encoding" note).
cd /d "%~dp0"

if not exist build mkdir build
javac -encoding UTF-8 --release 21 -d build src\*.java
if errorlevel 1 exit /b 1

echo Compiled classes to build\.
echo Run it:      java -cp build Main -h
echo Self-test:   java -cp build Main --selftest
