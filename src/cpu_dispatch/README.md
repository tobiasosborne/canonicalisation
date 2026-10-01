# src/cpu_dispatch

**Purpose.** Runtime selection of scalar-equivalent ISA kernels.

**Governing spec sections** (`docs/specification.md`, normative): §13 (CPU kernels and optional GPU boundary), §22 (initial engineering defaults).

**Milestone.** Filled by **M8** (`docs/implementation-plan.md`). Nothing is implemented here yet; this directory is a placeholder.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
