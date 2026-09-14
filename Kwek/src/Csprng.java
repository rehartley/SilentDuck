// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

import java.security.SecureRandom;

// Csprng -- direct OS CSPRNG access for OTP key material, playing the same
// role as Kwak's SecureRandom.h/.cpp (which itself stands in for Quacque's
// QRandomGenerator::system() and otp.py's os.urandom()). Named Csprng, not
// SecureRandom, purely to avoid shadowing java.security.SecureRandom in
// files that import it -- same security tier that name implies, not a
// different one.
//
// java.security.SecureRandom's no-arg constructor is itself backed directly
// by the platform's CSPRNG (Windows-PRNG/CryptGenRandom-family on Windows,
// NativePRNG over getrandom(2)/\/dev\/urandom on Linux, etc.) via the JCA's
// default provider -- so unlike Kwak, which has to reach past
// std::random_device (not guaranteed non-deterministic by the standard) to
// BCryptGenRandom/\/dev\/urandom by hand with an #ifdef, there is no
// separate "go around the standard library to the OS API" step here. See
// ../kwek_design.md.
final class Csprng {
    private Csprng() { }

    private static final SecureRandom RNG = new SecureRandom();

    // Fills buf[0..buf.length) with cryptographically secure random bytes.
    static void fillBytes(byte[] buf) {
        RNG.nextBytes(buf);
    }

    // Returns a uniformly distributed digit 0-9 with no modulo bias, via
    // rejection sampling -- same approach, same reasoning, as Kwak's
    // secure_random::randDigit(): a raw random byte is 0-255, and 256 isn't
    // a multiple of 10, so naively taking byte % 10 would make digits 0-5
    // (the ones below 256 % 10 == 6) very slightly more likely than 6-9.
    // Rejecting any byte >= 250 (the largest multiple of 10 that fits in a
    // byte) before taking % 10 removes that bias, at the cost of very rarely
    // drawing an extra byte. Deliberately not RNG.nextInt(10): that call is
    // documented to be unbiased too, but doing it by hand keeps this the same
    // auditable four-line primitive in every port (otp.py, Quacque, Kwak,
    // now Kwek) rather than trusting a different library's internal claim in
    // each one.
    static char randDigit() {
        byte[] b = new byte[1];
        int v;
        do {
            RNG.nextBytes(b);
            v = b[0] & 0xFF;
        } while (v >= 250);
        return (char) ('0' + (v % 10));
    }
}
