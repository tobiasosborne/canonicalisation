# src/perm

**Purpose.** Permutation arrays and their primitives: composition, inverse, support, action on lists.

**Governing spec sections** (`docs/specification.md`, normative): §3 (array and product convention `p[v]=v^p`, `(pq)[v]=q[p[v]]`), §9 (group kernel).

**Milestone.** Filled by **M2** (`docs/implementation-plan.md`). Real code today: only the declarations in `perm.h` (no implementation).

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
