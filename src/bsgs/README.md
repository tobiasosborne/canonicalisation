# src/bsgs

**Purpose.** Base/strong-generating-set chains, sifting, stabilisers, base change, verification, canonical group bytes.

**Governing spec sections** (`docs/specification.md`, normative): §9.1 (reference construction and certification), §9.2 (costs, provenance), §9.3 (physical representations), §9.4 (Group(H) grammar), §11 (capacity).

**Status.** Implemented in S1: the group interface `group.h` (`canon_group_ops`: `order`, `contains`, `tuple_min` returning the least `L^t`, the least such `t`, and the spec §7.1-ordered orbit ids of `G_M` on target labels) behind the public `canon_group` handle, `group.c` (`canon_group_alloc`, the only constructor of a handle, which sets the atomic reference count to one; `canon_group_share`/`unshare` and the public retain/release), and the explicit-enumeration backend `explicit.h`/`explicit.c` (closure by breadth-first generation, capacity checked before each growth, table sorted lexicographically; O(|G|·n) per query). Slice **S3** replaces the backend by a verified stabiliser chain behind the same interface (spec §9.1). Tests: `tests/c/test_group_explicit.c`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
