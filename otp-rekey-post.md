# Work factor of recovering two one-time-pad sheets from a checksum-sorted combinatorial rekeying broadcast

## Background and goal

*(Update: Sorry for the confusion - I realise my initial two versions of this post were somewhat unclear, based on the questions it prompted. This one should be much closer to what O was trying to say, and to help things along, you can check out the Python reference implementation which includes a manual walk through of what we are discussing here. it has also had some re-introduced bugs cleaned up and corrected.  Thank you for your feedback, this is obviously my first foray in this domain, It can be found at: https://github.com/rehartley/SilentDuck .)*

I implemented a manual (pencil-and-paper) one-time-pad system of a mid-1970s historically known type, with a Python reference tool (`otp.py`) that automates the same procedure a person would do by hand — every step below is a digit-wise mod-10 add/subtract, repeatable with pencil and the two physical key sheets. The piece I want checked is its rekeying step: two parties who already share a small amount of pre-distributed secret data (two OTP sheets) use it to deliver a much larger fresh working pad to each other over an open, observed channel, without meeting again and without a computer being required in the field.

The question is the computational work factor required to recover the two shared sheets — and therefore the delivered pad — from what's actually broadcast, with and without later known plaintext. Everything below (construction, numbers, claims) is read directly off the current `otp.py` and independently re-verified this session by running the real functions (`combinateExpandedKeys()`, `do_joinKeys()`, `do_unjoinKeys()`, `randDigits()`) against fresh random input, not by re-deriving on paper and hoping. I'm happy to share the verification scripts.

## The construction

**Shared secret:** two pre-shared sheets, $k_i$ and $k_j$. Each is 10 rows $r_0,\dots,r_9$ of 25 digits (250 digits/sheet, 500 digits total).

For a 5-element subset $S \subseteq \{0,\dots,9\}$, define the **row value**

$$e_S = -\sum_{t \in S} r_t \pmod{10}$$

(digit-wise, no carry between the 25 positions). Folding $e_S$'s five 5-digit groups together the same way (digit-wise, no carry) gives a 5-digit **checksum** $c_S$. Every one of the $\binom{10}{5}=252$ subsets $S$ (fixed, public, lexicographic order) gets expanded this way, for both sheets independently — 252 rows of $(e_S, c_S)$ per sheet.

**Combining the two sheets** (`do_joinKeys()`): the 252 rows from $k_i$ and the 252 rows from $k_j$ are pooled into one list of 504, **sorted by checksum** (collisions broken deterministically: bump to the next checksum value mod 100000, in encounter order — $k_i$'s rows first, then $k_j$'s), then split at the midpoint. Rank $r$ (in the sorted 504) is paired with rank $r+252$, for $r = 0,\dots,251$, and the two $e$ values are added digit-wise mod 10. This gives $K$: 252 rows × 25 digits = 6300 digits.

A fresh pad $R$ (6300 digits, drawn from the OS entropy source, 25 digits at a time) is generated, and the sender broadcasts

$$C = K - R \pmod{10}$$

The receiver, holding the same $k_i,k_j$, recomputes $K$ deterministically and recovers $R = K - C$. Only the first 6250 of $R$'s 6300 digits get written out as the 25 new key sheets that are actually used afterward — the remaining 50 digits are generated and consumed in the masking step but never saved as usable key material, identically on both ends, so this doesn't desynchronize sender and receiver.

**What an eavesdropper actually sees:** $C$ (6300 digits, every rekeying event), and — per the operational scenario this exists for — eventually traffic enciphered under $R$, which can supply known plaintext. Everything else is public: the combo enumeration, the checksum and pairing rule, the random-digit generator, all of `otp.py`. The only secret is the 500 digits of $k_i,k_j$.

## Structural facts about $K$ as a function of $(k_i, k_j)$

These bear directly on how hard $C$ is to invert, since $C$ only ever depends on $k_i,k_j$ through $K$. All verified this session against the live `combinateExpandedKeys()`/`do_joinKeys()`, on real random sheets:

- **The checksum only depends on a 5-digit fold of each contributing row, not its full 25 digits.** Precisely, $c_S = -\sum_{t\in S}\mathrm{fold}(r_t) \pmod{10}$, where $\mathrm{fold}$ reduces a row to 5 digits the same way $e_S$ is folded into $c_S$. Confirmed exactly, for all 252 combos, on real sheets. So the sort key that decides which rows land on which side of the median — and hence which two rows of $K$ get paired and added — is drawn from a $10^5$-value space, far coarser than the $10^{25}$-value space the row content itself lives in.
- **Pooling before sorting genuinely mixes the two sheets.** Over 200 random $(k_i,k_j)$ pairs, the low-checksum half (`keysA`) contained rows that actually originated from $k_j$ **50.0% of the time on average**, and symmetrically for `keysB`/$k_i$. So $K$'s rows are not "sheet $i$'s rows paired with sheet $j$'s rows in a fixed way" — which row pairs with which is itself a secret-dependent function of both sheets, decided by the joint sort.
- **Checksum collisions average ~1.25 per rekeying event** (504 draws into $10^5$ buckets; birthday estimate $\approx 1.27$, matches), but with a longer tail than that estimate alone would suggest — one run of 200 trials saw as many as 21. The 252 checksums from a single sheet aren't mutually independent (all built from only 10 underlying row-folds via overlapping 5-subsets), which is a plausible source of the extra spread.
- **Shifting every digit of every row of one sheet by the same even constant (0, 2, 4, 6, 8, mod 10) leaves that sheet's entire 252-row table — data and checksums — byte-identical**, verified exactly; the five odd shifts (1,3,5,7,9) collapse onto one shared alternate table, also byte-identical to each other. Since `do_joinKeys()` only ever consumes this table, that means $K$ (and hence $C$, for fixed $k_j$ and $R$) is *exactly* the same for any of the 5 even-shift variants of $k_i$. This is a genuine, permanent ceiling on what any observation of the real protocol's output could ever pin down about $k_i$ — not a big one (5 candidates per sheet is nothing next to the ~1661-bit budget of the shared secret), but it's exact and worth having on record rather than assumed.
- Round-trip correctness: `do_unjoinKeys()` recovered the receiver's pad byte-for-byte identical to what `do_joinKeys()` generated, across a live run on real files.

## Questions

1. What's the best known attack to recover $k_i,k_j$ (equivalently $K$, equivalently $R$) from $C$, given the full construction above — is the checksum-sorted, rank-paired combination step (pool both sheets' 504 rows, globally sort, pair rank $r$ with rank $r+252$) reducible to known turnpike/beltway (partial-digest) results, or does the secret-dependent pairing put it in a different class?
2. Does pooling both sheets before sorting (so each output half is a ~50/50, secret-dependent mix of both sheets, as measured above) meaningfully change the work factor relative to combining same-indexed rows directly — net help, net hurt, or wash?
3. Under known plaintext — a later message enciphered under $R$ gets compromised, giving some rows of $R$ and hence, via $C$, the matching rows of $K = C + R$ — does that let an attacker unwind $k_i,k_j$ appreciably faster than ciphertext-only, particularly using the row-difference recovery available from any set of known $K$-rows (subtracting the value of two combos differing by one swapped row cancels everything but that one row's contribution)?
4. Is there existing literature on recovering a hidden set from a *rank-sorted* pairing of its subset sums with another hidden set's — as opposed to the plain single-sheet subset-sum/turnpike problem — that this reduces to or resembles?

This isn't expected to be unbreakable, and I'm not asking for a blessing — I'd like to know whether recovering $k_i,k_j$ from what's actually broadcast is trivial, or sits at a work factor that's meaningful against the ~1661-bit budget of the shared secret. Concrete attacks, reductions to known results, or a clear argument for why a given avenue doesn't help are all useful.
