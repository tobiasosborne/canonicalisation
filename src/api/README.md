# src/api

**Purpose.** Public entry points, handle lifecycle, status and result flags, wrappers.

**Governing spec sections** (`docs/specification.md`, normative): §17 (API, ownership, failures), §3.2 (status and flags), §3 (objectives).

**Milestone.** Filled by **M4** (`docs/implementation-plan.md`). Real code today: `version.c` (implements `canon_version`) and `stubs.c` (every §17 entry point returning `CANON_UNSUPPORTED_ACTION`). Public declarations live in `include/canon/canon.h`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
