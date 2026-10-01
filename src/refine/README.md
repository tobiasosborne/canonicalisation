# src/refine

**Purpose.** The P1 refinement stages O and G, fixed-point loop and trace tokens.

**Governing spec sections** (`docs/specification.md`, normative): §7.1 (stages O/G), §7.2 (trace bytes), §10 (refinement implementation), §15 (stronger refiners gated by new profile IDs).

**Milestone.** Filled by **M4** (`docs/implementation-plan.md`). Nothing is implemented here yet; this directory is a placeholder.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
