# Plan: getting real expert scrutiny (not a celebrity thumbs-up)

## Temper the goal first

Bruce Schneier has repeatedly written about what's often paraphrased as
**"Schneier's Law": anyone can design a cipher they themselves can't
break — that doesn't mean it's secure.** He receives a steady stream of
"please review my homebrew crypto scheme" requests and has said, many
times (blog posts, comments, essays), that he doesn't do individual
reviews of amateur schemes. Not rudeness — it's a principled position:
a single expert's blessing isn't how cryptographic validation actually
works. AES and SHA-3 became trusted through years of open competition
with dozens of independent cryptanalysts trying to break them, not one
authority saying "looks good."

**Realistic expectation:** a cold email or blog comment asking him
personally to check the scheme and give a thumbs-up will most likely get
silence, or a polite pointer back to exactly that principle. Plan around
getting *real scrutiny*, not a celebrity endorsement — the former is both
more achievable and more valuable anyway.

## Concrete steps, roughly in order of effort vs. payoff

1. **Write it up cleanly first.** The raw material already exists:
   - `manual_otp.md` — the by-hand worked example
   - `combinometrics_analysis.py` — the null-space / residual-ambiguity
     verification
   - This conversation's analysis (the mod-5 degeneracy proof, the
     two-sheet non-separability property, the ct-security argument)

   Condense into a short, self-contained technical writeup: the
   construction, the threat model, the null-space proof, the `5^25`
   residual-ambiguity bound, why the transmitted ciphertext is
   unconditionally secure regardless of the derived table's structure,
   and why `ki`/`kj` must be single-use and destroyed immediately. A
   rigorous writeup is what gets taken seriously; "check out my OTP tool"
   without the analysis attached reads exactly like the submissions
   Schneier is tired of seeing.

2. **Cryptography Stack Exchange.** Real working cryptographers answer
   there, often within days, for free. Post the construction itself (the
   5-of-10 combinatorial expansion, the null-space/mod-5 degeneracy, the
   two-sheet non-separability property) as a focused technical question —
   "is this secret-renewal scheme sound, here's my analysis, what am I
   missing." Best return on effort available.

3. **r/crypto.** Less rigorous than Crypto.SE but wider reach; occasional
   real cryptographers weigh in.

4. **Cryptologia.** A real, long-running peer-reviewed academic journal
   specifically about the history and technical analysis of
   manual/classical/field cryptography (Enigma, VIC cipher, numbers
   stations, OTP tradecraft). A scheme built specifically for
   pencil-and-paper field use, with rigorous null-space analysis behind
   it, is exactly their scope — a far better fit, and far more credible
   if ever cited, than a celebrity endorsement.

## The book angle

The real Schneier declining to rubber-stamp anything, while insisting on
genuine scrutiny, is a better and more authentic story beat than a simple
thumbs-up. A fictional stand-in who refuses to bless the scheme outright,
but concedes the specific narrow claim holds up under his own analysis —
that's exactly what the real person would actually say, and it stays true
to the real 2002 essay already quoted at the top of `otp.py`.
