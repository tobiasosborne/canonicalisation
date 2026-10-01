# refs/ : M0 blind reference implementations

**Purpose.** Plan milestone M0 and spec §20: before semantics are declared frozen, two independently written reference implementations of the P1, CDAG-2 and API rules must agree on traces, bytes, signs, statuses under fixed capacity and (where requested) deterministic witnesses, including generated cases beyond oracle range, and must detect a planted corruption. A separate tiny oracle enumerates everything exhaustively on small domains.

**Layout.** `ref-a/`, `ref-b/` (the blind implementations), `oracle/` (exhaustive enumeration), `compare/` (interchange format and comparison script), `vectors/` (machine-readable golden cases from spec §7.4).

## The blind protocol

1. **ref-a and ref-b are written by different people or agents, from the specification alone** (`docs/specification.md`: §§3, 4, 7, 8, 11, 17 at minimum). They share **no code**.
2. **No exchange of implementation choices before comparison.** Neither author reads the other's directory, notes or transcripts, and neither reads `checks/review_checks.py` (a coordinator-owned sanity model, not a blind reference) or the `expected_*`/`payload_hex` fields of `refs/vectors/golden.json` until their own implementation is complete (they may read the input fields: `n`, generators, objects); results must be **derived from the rules, not copied** (spec §7.4, §20).
3. **Sealing.** When an implementation is complete, its directory is sealed by recording the commit hash that contains it in `refs/SEALS.md` (implementation, language, author, sealed commit, date). Changes after sealing need a new row, never an edit.
4. **Comparison.** Each implementation emits the line-oriented format in `compare/FORMAT.md`; `compare/compare.py` reports per-case agreement and exits 1 on any disagreement. The comparison includes `--plant`, a planted-corruption run showing the comparison is sensitive (spec §20).
5. **Divergences are reviewed as spec ambiguities first.** A disagreement is not settled by deciding which author is right; the coordinator reads the spec sentence both claim to implement, and fixes the spec text when it is ambiguous, before any code is changed.
6. **The oracle** (`oracle/`) enumerates groups, orbits, minima, complete stabilisers, transporters, characters and reference tree keys exhaustively on tiny domains. It checks oracle-sized cases; ref-a and ref-b must also agree with each other on larger generated cases beyond oracle range.
7. **Retention.** Both implementations, their seeds and generated case lists are retained (spec §20). Unrebuildable prototype reports cannot satisfy the gate.

M0 may use any development language; it does not define the production dependency set. Passing M0 requires hand-derived vectors reproduced, trace/byte/status/sign agreement, and a detected planted convention/sign/encoding error. Agreement is evidence about ambiguity and bugs, not a proof of correctness.
