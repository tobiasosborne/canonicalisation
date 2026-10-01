# src/encoding

**Purpose.** CDAG-2 wire encoder/decoder, canonical DAG numbering, order keys.

**Governing spec sections** (`docs/specification.md`, normative): §4.1 (wire grammar), §4.2 (canonical DAG references), §4.3 (comparison and keys), §4.4 (SIMPLE-UPPER-1), §9.4 (Group payload).

**Milestone.** Filled by **M3** (`docs/implementation-plan.md`). Nothing is implemented here yet; this directory is a placeholder.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
