# src/perm

**Purpose.** Permutation arrays and their primitives: composition, inverse, support, action on lists.

**Governing spec sections** (`docs/specification.md`, normative): §3 (array and product convention `p[v]=v^p`, `(pq)[v]=q[p[v]]`), §9 (group kernel).

**Status.** Implemented in S1: `perm.h`/`perm.c`, dense image arrays with `canon_perm_compose` (`out = pq`), `canon_perm_inverse`, `canon_perm_apply_tuple` (right action on a list), `canon_perm_lex_compare`, `canon_perm_is_identity`, `canon_perm_validate_scratch` (range and bijection with a caller bitmap) and `canon_perm_validate` (allocating wrapper; returns -1 on allocation failure). Sparse permutations and the tagged identity of the detailed plan §2.2 remain for **M2**/S3. Tests: `tests/c/test_perm.c`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
