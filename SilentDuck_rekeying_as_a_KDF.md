# SilentDuck: Rekeying as a KDF

## The core reframing

The scheme's central idea is easy to state once the right frame is in place: **a shared, public, time-varying artifact is used not as a source of secrecy, but as a synchronization pointer.** Two parties who hold a common short secret can independently derive fresh, identical key material each period by feeding that secret and the current public artifact into a keyed one-way function. Nothing new is transmitted between them; the world advances the shared input on its own.

Written as one line, the whole scheme is:

```
key_material = KDF(shared_secret, public_nonce)
```

where `public_nonce` is derived from something both parties can independently obtain — this week's cover story in a named periodical, a published tide table, a satellite's ephemeris, the finishing order of a race, the exact wording of a particular news outlet's lead article. The `shared_secret` carries all of the security. The `public_nonce` carries all of the *non-repetition*.

## Why it works: the source is an index, not an entropy source

The tempting but wrong mental model is that we are "extracting randomness" from the public text. We are not, and this matters. To an adversary, a Bible, a tide table, or a *Town & Country* cover story has essentially zero entropy — everyone has access to it. If secrecy depended on the source being unpredictable, the scheme would be worthless.

The correct model is the one that unlocks the whole design: **a good keyed pseudorandom function produces output that is computationally indistinguishable from random regardless of its input, provided the key stays secret.** Feed it zeros, feed it scripture, feed it gossip — the output is pseudorandom keystream either way, because the randomization comes from the keyed function, not from the input.

This is the "zeros" intuition, and it is the crux. If `KDF(secret, all_zeros)` already yields usable pseudorandom keystream, then the input's *content* was never doing cryptographic work. So what is the input *for*? It is for **change**. `KDF(secret, all_zeros)` is a fixed stream — to get fresh bits we would have to change the secret, which reintroduces the key-distribution problem we are trying to eliminate. `KDF(secret, this_week's_public_artifact)` gives fresh keystream every period **without either party sending anything**, because the public artifact changes by itself.

The input's only two required properties are therefore:

- **Varying** — so the derived key material is fresh each period. (Zeros fail this.)
- **Agreeable without communication** — so both parties land on the same input without a transmission. (A private random string fails this — agreeing on it *is* the distribution problem.)

A public, evolving artifact is the sweet spot: varying by itself, agreeable without communication. Every property people instinctively demand of a "random source" — high entropy, unpredictability to the adversary — is irrelevant here, because the source provides synchronization, not randomness.

## This is HKDF with an unusual salt

The mechanism is not novel and does not need a new security proof. `key_material = KDF(shared_secret, public_nonce)` is the construction underneath essentially every serious deployed cryptosystem: TLS derives per-session keys this way, Signal's ratchet does it per message, and HKDF is precisely this pattern and is specified in an RFC. In KDF terms the public artifact simply plays the role of the **salt / nonce** — a value that was never secret, whose entire job is to make the output different each time.

What is genuinely new in SilentDuck is not the mechanism but the **choice of nonce**: using a shared evolving *public* artifact that both parties obtain independently from the world, rather than a nonce exchanged over a channel. The novelty is in the logistics, not the cryptography. The hard, historically unsolved problem was never the math — it was *delivery*. This routes around delivery.

## Shannon does not bite

The common objection ("Shannon says a one-time pad needs key entropy equal to message entropy") does not apply, because the scheme never claims information-theoretic perfect secrecy and does not need it. Shannon's theorem governs the relationship between a truly random, used-once keystream and the message. SilentDuck is a **stream cipher**: a short secret expanded deterministically into a long pseudorandom keystream. Its security is **computational**, resting on the KDF/PRF being a good one-way function — exactly the footing on which AES-CTR, TLS, and every modern stream cipher already stand. "Shannon notwithstanding" is correct: this is a different game from Shannon's, the same game every deployed cipher plays.

## Correctness: the three conditions

The scheme is sound if and only if three conditions hold. All three are satisfiable and checkable, and notably none of them require the nonce to be secret, high-entropy, or unpredictable.

1. **Both parties derive the identical nonce.** This is the *only* real engineering risk, and it is a canonicalization risk, not a cryptographic one. If both ends reduce "this week's cover story" to the exact same byte string, they derive identical keys; a one-character difference avalanches through the KDF and they share nothing. This is fully within our control — see below.

2. **The shared secret stays secret.** Standard assumption. The entire security reduces to this single, memorizable, never-transmitted value, which is exactly the property we want.

3. **The KDF is a real KDF.** Use HKDF or HMAC or another vetted construction. This is a *don't-be-clever* requirement.

## The one place to spend rigor: canonicalization

Because correctness hinges on both ends producing byte-identical nonces, the reduction from raw public artifact to nonce must be pinned precisely and deterministically. For text sources: OCR (if from print), then strip to lowercase letters only, drop punctuation and whitespace, optionally dictionary-correct, *then* hash the canonical string. Never hash raw pixels or raw OCR output — two scans or two OCR engines differ by a stray comma or an em-dash-read-as-hyphen, and the hash amplifies any single-bit difference into total keystream divergence.

**Image-based sources raise entropy but worsen fragility.** A digitized photograph carries far more raw material per page than caption text, but two photographs of the same printed photo do not match at the pixel level at all. Going image-based means canonicalizing hard — coarse downsampling, aggressive quantization, or perceptual-feature hashing rather than pixel hashing. Text remains the robust default; images are higher-entropy, higher-fragility. Same capacity/robustness/determinism triangle that governs the rest of the design.

**Add a verification hash.** Both ends should exchange or check a short hash of the derived nonce *before* trusting the new key material. This converts a silent, catastrophic desync into a detectable, recoverable one — the single most valuable piece of operational insurance in the whole scheme.

## Keyed, not bare

A subtle but non-negotiable point: the one-way function must be **keyed**. `SHA(public_text)` is reproducible by the adversary — they hold the text, they get the same digest. `HMAC(secret_key, public_text)` is what is required: the adversary has the text but not the key, so they cannot derive the keystream even knowing exactly which artifact was used. This is also *why the recipe can be published*. Kerckhoffs holds: the secret lives in the key, not in the choice of source. We could announce "we key off this week's named cover story" and remain unreadable.

---

## Operational benefits

The reason the scheme keeps opening up under inspection is that its constraints reinforce one another rather than trade off against one another — usually the signature of a real idea rather than a clever hack.

### The virtues chain together

The security requirement (secret lives in the key, Kerckhoffs-clean) *permits* publishing the recipe. Publishing the recipe *permits* the source to be fully public. A public source *is* what enables rekeying without transmission. Rekeying without transmission *is* what removes the courier — the historical failure mode. Removing the courier *is* what makes the channel survivable. Each property we want turns out to be **enabled by**, not paid for with, the others.

### Kills the distribution problem

Physical pad distribution's historical weakness was never the math — it was logistics. Pads get intercepted, photographed, seized, or reused under production pressure (the reuse that broke Soviet traffic in VENONA). A reseed-from-public-artifact scheme deletes the courier entirely. The KGB-style requirement that agents return periodically for face-to-face pad handover is replaced by both ends independently deriving the same fresh pad from something a billion people can obtain. What once needed a meeting and a physical handoff now needs only literacy and punctuality.

### Forward secrecy the courier pads never had

A stolen physical pad compromises every message it covers. Here, if one period's derived key material leaks, it burns **that period only**. Next period's material derives from next period's public nonce under the same secret, and the adversary who holds the nonce still lacks the key. The blast radius of any single compromise is one period — strictly better than the historical systems being imitated, not merely cheaper.

### Deniable at every layer simultaneously

- **Nothing secret is transmitted** — the nonce is public; the recipe can be public.
- **Nothing secret is stored** — the pad does not exist until derived, and evaporates after use.
- **Nothing secret is carried** — the periodical is innocent and matched to the carrier's persona.

The only secret is a short key held in one place: memorizable, deniable, nothing on paper. Every observable thing about the operation has an innocent explanation, and the one thing that does not is invisible.

### Source selection is a cover problem, not an entropy problem

Once entropy stops being the binding constraint (any candidate source has plenty), the real axis is **invisibility**. The source must fit the person carrying it. Magazines and tabloids beat newspapers on physical tradecraft: a single discreet photo of a glossy two-page spread yields thousands of characters of raw material, versus fumbling through a broadsheet — fewer photos, less exposure time, less conspicuous behavior. The library periodical rack is one of the few places where sitting and reading a magazine is the *expected* behavior.

**Pocket-litter consistency is the hard constraint.** If someone is stopped and searched, everything on them must tell one coherent story. A source incongruent with the persona (the *Financial Times* on the wrong reader, *Tattoo World* on the wrong reader) is not a crime — it is a *question*, and questions are what a trained observer pattern-matches on. The magazine is part of the cover identity and must be consistent with everything else about it.

**Persona-matching buys compartmentation for free.** Because the source must match the persona, different operatives naturally use different sources, so no single periodical becomes a signature linking a network. If everyone keyed off the same source, that source would be the tell. The cover requirement and the compartmentation requirement point the same way.

### The source generalizes past text

The "public, varying, agreeable-without-transmission" nonce is a *pattern*, not a specific medium. Any shared evolving public reference works, each with its own built-in cover story:

- A satellite's published ephemeris
- The day's tide tables
- A live-streamed chess grandmaster's game
- The finishing order of a scheduled race
- The verbatim lead article of a named news outlet
- The first celebrity-gossip article of the day (near-perfect uptime — the world reliably produces it in volume, so availability is never the failure mode; entropy lives in the verbatim wording, not the predictable topic)

Pairing sources with *orthogonal* failure modes strengthens the pool: a source unpredictable in both topic and wording, mixed with one whose topic is guessable but wording is not, means an adversary who anticipates one still cannot anticipate the other. The pool's floor is set by the least predictable input, not the average.

### Steerability is the axis to watch when choosing sources

Sources differ in how much an adversary can *shape* them. Scripted institutional events (a State of the Union, a fixed liturgical blessing) are low-entropy and, worse, potentially known in advance to a capable service. Political coverage is the most *plantable* of all — a capable actor can time a leak or shape a cycle. Physically chaotic sources (tide residuals, RF noise, order-book microstructure) are the ones even a state cannot feasibly steer. The verbatim-hash still protects against steering (no one dictates the exact sentence), but a steerable source should never run *solo* — always pool it with something chaotic and unplantable.

### Sync failure wants a PACE ladder

Because the channel can desync (a missed period, a canceled source, a canonicalization disagreement), it needs a pre-agreed degradation ladder — Primary, Alternate, Contingency, Emergency — frozen in the initial keying material so both ends silently arrive at the same rung without negotiating. The escalation trigger must be crisp and time-bound ("primary is dead if not confirmed within N periods; on N+1 both parties are automatically on Alternate, no handshake"). The Emergency rung must depend on **nothing** derived — only on the initial secret and a fixed rule — because it is invoked precisely when everything derived has failed.

---

## The bottom line

The rekey scheme works because it **is** HKDF with an unusual salt, and HKDF works. It introduces no new cryptographic mechanism requiring proof. What it introduces is a new *logistics* for a proven mechanism — a way to feed a standard KDF a fresh salt each period that both parties obtain from the world instead of from each other. The cryptography was always solid; the unsolved problem was always delivery, and the scheme routes around delivery. That is why it holds together: it starts from the right question — *what leaves no signal?* — and lets the tradecraft and the math become the same object.
