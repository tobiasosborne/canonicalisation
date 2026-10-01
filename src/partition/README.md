# src/partition

**Purpose.** Ordered partitions, cell splitting and individualisation with rollback.

**Governing spec sections** (`docs/specification.md`, normative): §7.1 (exact scalar rules for P1), §10 (refinement implementation and termination), §11 (rollback).

**Milestone.** Filled by **M4** (`docs/implementation-plan.md`). Nothing is implemented here yet; this directory is a placeholder.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
