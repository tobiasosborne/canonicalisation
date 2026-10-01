# refs/vectors

**Purpose.** Machine-readable transcription of every hand-checked case in `docs/specification.md` §7.4, used as inputs and expected outputs by tests and by the M0 comparison.

**Files.** `golden.json` has these sections: `p1_cases` (six P1 cases with `n`, generator images, object, expected trace hex, expected CDAG-2 stream hex, expected witness), `group_payload_cases` (Group(1), Group(Sym(2)), Group(C3), labeling coset H=1 r=[1,0]), `convention_vectors` (p, q, pq, qp, p^-1, cycle conjugation), `other_hand_checked` (typed labeling example, signed examples, CDAG-BYTE-1 minimum versus P1).

**Rules.** Hex is lowercase with no spaces. The spec's abbreviations H(n), A(a), S(ids), B0, T(ids) are expanded to full bytes. Every entry has a `source` field and the spec's own wording in `spec_text`. Permutations are arrays `p[v]=v^p`.

**Verification.** `tests/python/test_golden_vectors.py` recomputes every entry with `checks/review_checks.py`, so a transcription error fails the test.

**Blind-protocol warning.** The blind implementers (`refs/README.md`) must derive results from the rules. They may read the input fields (`n`, `group_generators`, `object`) but must not look at `expected_*`, `payload_hex` or other answer fields until their implementation is complete.

**Adding a vector.** See CONTRIBUTING.md ("How to add a golden vector").
