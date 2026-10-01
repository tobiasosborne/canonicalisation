# src/bsgs

**Purpose.** Base/strong-generating-set chains, sifting, stabilisers, base change, verification, canonical group bytes.

**Governing spec sections** (`docs/specification.md`, normative): §9.1 (reference construction and certification), §9.2 (costs, provenance), §9.3 (physical representations), §9.4 (Group(H) grammar), §11 (capacity).

**Status.** Implemented in S1: the group interface `group.h` (`canon_group_ops`: `order`, `contains`, `tuple_min` returning the least `L^t`, the least such `t`, and the spec §7.1-ordered orbit ids of `G_M` on target labels) behind the public `canon_group` handle, `group.c` (`canon_group_alloc`, the only constructor of a handle, which sets the atomic reference count to one; `canon_group_share`/`unshare` and the public retain/release), and the explicit-enumeration backend `explicit.h`/`explicit.c` (closure by breadth-first generation, capacity checked before each growth, table sorted lexicographically; O(|G|·n) per query). Implemented in S3 (`docs/slices/S3.md`, notes in `docs/slices/S3-notes.md`), behind the unchanged interface:

- `chain.h`/`chain.c`: the stabiliser chain (levels with base point, orbit in discovery order, `orbit_pos`, Schreier vector over orbit positions, inclusive generator lists; dense strong generators with their inverses; provenance ids; `uint64` order) and the deterministic Schreier–Sims constructor of §9.1 with the policies of the S3 brief §2.2 (input normalisation, least-moved-point base extension, insertion at the sift's stop level, recomputation of the changed levels `0..j`, deepest-first closure restarting at the insertion level, strict growth); transporter reconstruction `t_b = s_0 s_1 … s_(k-1)`; sifting (`g ← g t_b⁻¹`); allocation-free membership; suffix (point-stabiliser) orders; `rebase` (rebuild from a level's strong generators with a requested base prefix, then verify); orbit ids of a pointwise stabiliser in §7.1 order; the §7.2 tuple minimum (returns the least minimiser and the orbits of `G_M`). Construction stops with `CANON_CAPACITY_LIMIT` as soon as the orbit-length product (a lower bound on `|G|`) overflows `uint64`. Counters in `canon_bsgs_stats`.
- `verify.h`/`verify.c`: the independent verifier of §9.1 (written before `chain.c`, reading only the layout, `perm.h` and `provenance.h`), with one reason per violated condition; it also checks that generator lists are nested, which the §9.1 induction needs (see the notes).
- `provenance.h`/`provenance.c`: straight-line records `INPUT i | INVERSE j | PRODUCT j k` (§9.2), evaluated in one forward pass.
- `chain_backend.h`/`chain_backend.c`: `canon_group_ops` over a verified chain; the default backend of `canon_group_create`.
- `reference.h`/`reference.c`: the direct Schreier recursion of §9.1 (ordered base `0..n-1`), for tests only.

The explicit backend remains as the test oracle (`canon_group_is_explicit`). Tests: `tests/c/test_group_explicit.c`, `test_chain.c`, `test_verify.c`, `test_provenance.c`, `test_reference_schreier.c`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
