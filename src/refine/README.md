# src/refine

**Purpose.** The P1 refinement stages O and G, fixed-point loop and trace tokens.

**Governing spec sections** (`docs/specification.md`, normative): §7.1 (stages O/G), §7.2 (trace bytes), §10 (refinement implementation), §15 (stronger refiners gated by new profile IDs).

**Status.** Implemented in S1: `p1.h`/`p1.c`, the subset root partition (membership key), the §7.1 node loop with the O stage for a non-graph root (empty signatures, identity split) and the G stage through `tuple_min`, and the §7.2 tokens `NODE`, `STAGE_O`, `STAGE_G`, `LEAF`. Graph O-stage signatures are slice **S2**. Tested through the search tests.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
