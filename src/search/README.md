# src/search

**Purpose.** Canonical search tree: target-cell choice, leaf map, trace/byte comparison, pruning with objective-specific coverage rules.

**Governing spec sections** (`docs/specification.md`, normative): §7 (profile P1), §7.2 (leaf map and proof), §7.3 (pruning), §8 (complete reference algorithms).

**Status.** Implemented in S1: `p1_tree.h`/`p1_tree.c`, the unpruned P1 tree for a subset (target cell by (size, position), every member individualised, rollback by snapshot), the §7.2 leaf map and the `(trace, CDAG-2 bytes)` minimum with the least attaining leaf witness, and the §11.1 NODE quota (`CAPACITY_LIMIT`). Workspace arrays are sized to a capacity that only grows. S2 generalises the search to a root object (`canon_p1_search_run`): the leaf acts on the root into reusable image storage (`canon_root_act_into`) and writes the stream by kind; the subset path is byte-identical to S1. Review fixes: the leaf compares its trace with the best one first (§7.2) and builds `t_L`, the image and its stream only when the trace is not greater (counters `leaves` and `images`), and every run ends by clearing the image's borrowed graph tables, so no workspace member points into a released object. Pruning and certificates are slice **S7**; other objectives S4/S6. Tests: `tests/c/test_search_subset.c`, `test_search_graph.c`, `tests/python/test_e2e.py`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
