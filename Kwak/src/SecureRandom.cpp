// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#include "SecureRandom.h"

#include <cstdlib>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <cstring>
#endif

namespace secure_random {

#ifdef _WIN32

void fillBytes(unsigned char *buf, std::size_t count)
{
    // BCryptGenRandom with the system preferred RNG -- the modern,
    // CNG-based replacement for the older CryptGenRandom, and what
    // Qt's QRandomGenerator::system() itself calls on Windows.
    const NTSTATUS status = BCryptGenRandom(
        nullptr, buf, static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0 /* STATUS_SUCCESS */) {
        std::fprintf(stderr, "FATAL: BCryptGenRandom failed (status 0x%lx)\n",
                      static_cast<unsigned long>(status));
        std::abort();
    }
}

#else

void fillBytes(unsigned char *buf, std::size_t count)
{
    // /dev/urandom rather than getrandom(2) directly: every Linux
    // distribution, *BSD, and macOS all expose this device, whereas
    // getrandom() is Linux-only and needs a glibc/kernel version check to
    // call safely -- reading the device keeps this one implementation
    // portable across everything POSIX this project targets, at the cost of
    // one open() per process (not per call -- see the static FILE* below).
    static std::FILE *urandom = std::fopen("/dev/urandom", "rb");
    if (!urandom) {
        std::fprintf(stderr, "FATAL: could not open /dev/urandom: %s\n", std::strerror(errno));
        std::abort();
    }
    if (std::fread(buf, 1, count, urandom) != count) {
        std::fprintf(stderr, "FATAL: short read from /dev/urandom\n");
        std::abort();
    }
}

#endif

char randDigit()
{
    unsigned char b = 0;
    do {
        fillBytes(&b, 1);
    } while (b >= 250); // reject the biased tail -- see header comment
    return static_cast<char>('0' + (b % 10));
}

} // namespace secure_random
