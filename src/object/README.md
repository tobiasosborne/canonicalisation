# src/object

**Purpose.** Native object adapters (subset, tuple, graph, DAG nodes, relations) with exact action, equality and validation.

**Governing spec sections** (`docs/specification.md`, normative): §6 (native objects, auxiliaries), §4.2 (extensional normalisation), §5 (semantic independence).

**Status.** Implemented in S1: the subset object `subset.h`/`subset.c` (bitset plus sorted member list, duplicate atoms merged, out-of-range atoms invalid, the spec §7.1 membership key, the §2.1 action `x^g` as a sorted list, equality). Tuples, graphs and nested DAGs follow in S2/S5 (**M3**). Tested through `tests/c/test_search_subset.c` and `tests/python/test_e2e.py`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
