# src/bsgs

**Purpose.** Base/strong-generating-set chains, sifting, stabilisers, base change, verification, canonical group bytes.

**Governing spec sections** (`docs/specification.md`, normative): §9.1 (reference construction and certification), §9.2 (costs, provenance), §9.3 (physical representations), §9.4 (Group(H) grammar), §11 (capacity).

**Milestone.** Filled by **M2** (`docs/implementation-plan.md`). Nothing is implemented here yet; this directory is a placeholder.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
