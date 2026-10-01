# src/arena

**Purpose.** Admission-time allocation, live-memory ledger, bounded caches and workspace storage.

**Governing spec sections** (`docs/specification.md`, normative): §11 (capacity, rollback, all live memory), §12 (complexity ledger), §16 (exact caches).

**Milestone.** Filled by **M5** (`docs/implementation-plan.md`). Nothing is implemented here yet; this directory is a placeholder.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
