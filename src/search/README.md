# src/search

**Purpose.** Canonical search tree: target-cell choice, leaf map, trace/byte comparison, pruning with objective-specific coverage rules.

**Governing spec sections** (`docs/specification.md`, normative): §7 (profile P1), §7.2 (leaf map and proof), §7.3 (pruning), §8 (complete reference algorithms).

**Milestone.** Filled by **M4** (`docs/implementation-plan.md`). Nothing is implemented here yet; this directory is a placeholder.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
