# src/refine

**Purpose.** The P1 refinement stages O and G, fixed-point loop and trace tokens.

**Governing spec sections** (`docs/specification.md`, normative): §7.1 (stages O/G), §7.2 (trace bytes), §10 (refinement implementation), §15 (stronger refiners gated by new profile IDs).

**Status.** Implemented in S1: `p1.h`/`p1.c`, the subset root partition (membership key), the §7.1 node loop with the O stage for a non-graph root (empty signatures, identity split) and the G stage through `tuple_min`, and the §7.2 tokens `NODE`, `STAGE_O`, `STAGE_G`, `LEAF`. Implemented in S2: the refinement is generalised to a root object (`canon_p1_initial`, `canon_p1_refine_node` take a `canon_root`), the grow-only `canon_p1_scratch` is managed here (`canon_p1_scratch_reserve`), and the graph O stage computes sparse arc-count signatures over CSR/CSC against the entry snapshot (index `((l·k)+j)·2+dir`), compares them exactly as the dense §7.1 vectors with zeros elided (`canon_p1_sig_compare`, justification in `p1.c`), and splits by global signature rank. The per-point scratch arrays are one allocation carved from a single table (`canon_p1_points`), and the signature entries and their sort scratch are the two halves of one allocation. Tests: `tests/c/test_signature.c` (against a dense implementation in the test), the search tests.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
