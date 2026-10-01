# src/arena

**Purpose.** Admission-time allocation, live-memory ledger, bounded caches and workspace storage.

**Governing spec sections** (`docs/specification.md`, normative): §11 (capacity, rollback, all live memory), §12 (complexity ledger), §16 (exact caches).

**Status.** S1 adds only `checked.h`: overflow-checked `size_t` multiplication and addition used before every allocation (spec §11.1). The admission planner, live-memory ledger and workspace storage are slices **S8**/**M5**.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
