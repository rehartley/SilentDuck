// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#pragma once

#include <cstdint>

// SecureRandom -- direct OS CSPRNG access, replacing
// QRandomGenerator::system() (which itself wraps CryptGenRandom/BCrypt on
// Windows, /dev/urandom-equivalent elsewhere). Same security tier as
// otp.py's os.urandom() and Quacque's QRandomGenerator::system() -- no
// better, no worse; see OTP.h's comment on randDigit() in both ports. If a
// stronger, independently audited guarantee is ever wanted, the natural
// upgrade is still libsodium's randombytes_buf(), same as noted there.
//
// Deliberately NOT std::random_device: the standard only requires it to be
// "non-deterministic" if the implementation can manage it, and some libstdc++
// configurations historically fell back to a seeded PRNG when no hardware
// source was available -- not an acceptable tier for OTP key material. Going
// straight to the OS API sidesteps that implementation-defined gap entirely.

namespace secure_random {

// Fills buf[0..count) with cryptographically secure random bytes. Aborts the
// process (via std::abort()) on failure -- getting random bytes is not
// something this app can sensibly recover from or fall back on, the same
// judgment call otp.py/Quacque make implicitly by letting the underlying
// CSPRNG call's own exception/failure propagate.
void fillBytes(unsigned char *buf, std::size_t count);

// Returns a uniformly distributed digit 0-9 with no modulo bias -- the same
// property QRandomGenerator::bounded(10) guarantees internally (see OTP.cpp's
// randDigit() comment), reimplemented here by hand via rejection sampling: a
// raw random byte is 0-255, and 256 isn't a multiple of 10, so naively taking
// byte % 10 would make digits 0-5 (the ones below 256 % 10 == 6) very
// slightly more likely than 6-9. Rejecting any byte >= 250 (the largest
// multiple of 10 that fits in a byte) before taking %10 removes that bias
// at the cost of very rarely drawing an extra byte.
char randDigit();

} // namespace secure_random
