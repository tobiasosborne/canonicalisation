# src/encoding

**Purpose.** CDAG-2 wire encoder/decoder, canonical DAG numbering, order keys.

**Governing spec sections** (`docs/specification.md`, normative): §4.1 (wire grammar), §4.2 (canonical DAG references), §4.3 (comparison and keys), §4.4 (SIMPLE-UPPER-1), §9.4 (Group payload).

**Status.** Implemented in S1: wire primitives `wire.h`/`wire.c` (growable buffer, `U16`, `U32`, `B`, overflow-checked sizes, the §4.3 unsigned byte order) and `subset_stream.h`/`subset_stream.c` (the CDAG-2 stream of a top-level subset, a specialisation of the §4.2 numbering, plus its exact length). Implemented in S2: the `Nat` writer `canon_buf_put_nat` and `canon_nat_length` in `wire.c` (§4.1 shortest big-endian form), `graph_stream.h`/`graph_stream.c` (the CDAG-2 stream of a top-level graph, one height-0 record `09`; its exact length is measured once at import by `canon_graph_stream_measure` and cached on the graph), and `simple_upper.h`/`simple_upper.c` (the §4.4 `SIMPLE-UPPER-1` key for uncoloured simple undirected graphs, by binary search over the sorted arcs, so it needs no index; the minimum search under that order is S4). The general DAG normaliser, decoder and validator are slice **S5** (**M3**). Tests: `tests/c/test_wire.c`, `test_nat.c`, `test_graph_stream.c`, `test_simple_upper.c`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
