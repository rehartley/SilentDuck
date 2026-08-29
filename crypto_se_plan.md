# Cryptography Stack Exchange post draft

Ready-to-post text for Crypto.SE (Markdown + MathJax). See `schneier-plan.md`
for the broader outreach plan this fits into.

---

**Title:** Security of a combinatorial key-renewal scheme for manual (pencil-and-paper) one-time-pad key distribution

**Body:**

I implemented a scheme for renewing one-time-pad key material between two parties who already share a small amount of pre-distributed secret data, without meeting again in person, and without requiring a computer to execute (it must be doable by hand, digit-wise, in the field). I've done my own analysis below and would like it checked — I'd specifically like to know if there's an attack I'm missing, or if my security argument has a gap.

**Construction**

Both parties hold two previously-shared OTP sheets, $k_i$ and $k_j$, each consisting of 10 rows of 25 truly random decimal digits (generated from a real entropy source), for 250 digits total per sheet.

Let $S \subset \{0,\dots,9\}$ range over all $\binom{10}{5}=252$ five-element subsets. For a sheet with rows $r_0,\dots,r_9$ (each a 25-digit vector), define, for each subset $S$ and each of the 25 digit positions independently:

$$e_S = -\sum_{t \in S} r_t \pmod{10}$$

This gives 252 "expanded" rows per sheet. Both sheets are expanded this way using the *same* enumeration of subsets, and the two expansions are combined **digit-wise** (not concatenated):

$$c_S = e_S^{(k_i)} + e_S^{(k_j)} \pmod{10}$$

giving one 252-row combined table $c$. A fresh batch of true-random data $r$ (the new key material to be delivered — sized to match the 252-row table) is generated, and the sender transmits:

$$\text{ct} = r - c \pmod{10}$$

over an insecure channel (e.g. broadcast). The recipient, holding their own copies of $k_i, k_j$, independently recomputes $c$ and recovers $r = \text{ct} + c$. Afterward, both parties destroy their copies of $k_i$ and $k_j$ — they are single-use.

**My analysis so far**

1. *Transmission security:* Since $r$ is fresh, uniformly random, secret, and used exactly once, $\text{ct} = r - c$ is information-theoretically independent of $c$ for a passive eavesdropper — the standard OTP argument, and it holds regardless of $c$'s internal structure. So I believe the actual broadcast is unconditionally secure as long as $r$ meets those conditions, independent of everything below.

2. *What happens if $c$ itself is exposed* (e.g., through a separate compromise, not via the broadcast): because $c_S$ is a fixed linear function (mod 10) of only 10 unknowns (a sheet's rows) per position, an attacker who obtains many rows of $c_S$ can recover every pairwise difference between rows exactly (via subtracting two subsets that differ by one swapped element), but I found the absolute value of any row is never recoverable: substituting the recovered differences into any subset-sum equation always reduces to $5x \equiv k \pmod{10}$, and since $\gcd(5,10)=5$, this only constrains $x$'s parity, never its value. Concretely: shifting every row of a sheet by the same even digit (mod 10) leaves every one of the 252 combination rows completely unchanged, for any digit position. I've verified numerically that this leaves exactly $5^{25}$ candidate sheets consistent with full knowledge of a sheet's own 252-row expansion, and that no additional leaked rows shrink this further once the difference-graph is fully connected.

3. *Combining $e^{(k_i)}$ and $e^{(k_j)}$ digit-wise, using the same subset per position*, makes $c_S$ mathematically identical to expanding a single virtual sheet $z$ where $z_t = (r_t^{(k_i)} + r_t^{(k_j)}) \bmod 10$. So I believe $c$ never exposes $k_i$ or $k_j$ individually, even in the worst case of total leakage of $c$ — only their sum, which inherits the same $5^{25}$-per-position bound from (2). Each digit of $z$ has 10 equally-valid $(k_i, k_j)$ splits, so I don't think separating them is a computational question at all, just information-theoretically absent — but I'd like that checked.

**Numerical verification of (2) and (3)**

I wrote a from-scratch brute-force check against a real, randomly-generated 10-row sheet (not a toy/reduced example) rather than just asserting the claims above. Summarized output:

```
Shifting every row of a sheet by the same EVEN digit (0,2,4,6,8):
  output identical to the unshifted sheet's 252-row expansion?  True (all five)
Shifting every row by the same ODD digit (1,3,5,7,9):
  output identical to a shift-by-1 sheet's expansion?           True (all five)
  (i.e. shifting by an odd digit lands on a different but still-shared output)

Recovering pairwise row differences from leaked combination rows:
  all 10 rows connected via recovered differences:  True
  recovered differences match ground truth exactly: True

Brute-forcing each of the 25 digit columns independently against
all 252 real combination-row equations:
  surviving candidate digits per column:  5 (out of 10), for every column
  total candidate key sheets consistent with FULL leakage of the
  entire 252-row expansion:  5^25 = 298,023,223,876,953,125
```

So: full leakage of a sheet's complete 252-row expansion narrows the sheet down to ~59 bits of remaining ambiguity, concentrated entirely in the even/odd null space identified in (2) — never further, regardless of how many rows are leaked. I'm happy to share the verification script itself if that would help.

**Questions**

- Is the reasoning in (1) complete, or is there a subtlety in relying on a single fresh OTP mask over *derived* (non-independently-generated) data that I'm not accounting for?
- Is the null-space argument in (2) — and the $5^{25}$ bound — correct? Is there a smarter linear-algebraic or other attack against this specific "$k$-of-$n$ subset-sum" structure that recovers more than pairwise differences?
- Is (3)'s non-separability claim actually watertight, or is there a way to exploit reusing the *same* subset enumeration across both sheets that I'm missing (e.g. some cross term I haven't accounted for)?
- Is there existing literature on this general class of construction (linear/subset-sum-based secret expansion, as opposed to a stream cipher) that I should be citing or checking against?
