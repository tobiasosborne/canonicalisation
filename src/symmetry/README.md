# src/symmetry

**Purpose.** Verified automorphism discovery and the known-automorphism subgroup used for pruning.

**Governing spec sections** (`docs/specification.md`, normative): §7.3 (pruning and A_known), §8 (objective-specific completeness), §8.4 (signed canonical images).

**Milestone.** Filled by **M4** (`docs/implementation-plan.md`). Nothing is implemented here yet; this directory is a placeholder.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
