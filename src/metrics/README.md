# src/metrics

**Purpose.** Instrumentation counters and the cost ledger inputs.

**Governing spec sections** (`docs/specification.md`, normative): §12 (complexity ledger, reproducible gaps), §19 (benchmark programme and metrics).

**Status.** Slice S8 (`docs/slices/S8.md` §1.5) delivers the counters as `canon_metrics` on every result (`canon_result_metrics`, `include/canon/canon.h`), collected in `src/api/api.c` from the search and enumeration states (`src/search/`, `src/symmetry/`, `src/coset/`) and from the ledger (`src/arena/mem.h`): P1 nodes, leaves, images, explored and pruned children, automorphism candidates and insertions (explicit and implicit), stabiliser rebases, enumeration visits and leaves, stabiliser hits and verified rebuilds, coset descents and transient rebuilds, character evaluations, the signed fast path, the root-discrete and singleton-orbit indicators of spec §7.3, output and certificate bytes, the planned bytes and the measured live-memory peak. No times: no benchmark before M5 (HANDOFF). This directory stays a placeholder for the M5 ledger code (per-stage times, cache and hardware counters).

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
