# src/arena

**Purpose.** Admission-time allocation, live-memory ledger, bounded caches and workspace storage.

**Governing spec sections** (`docs/specification.md`, normative): §11 (capacity, rollback, all live memory), §12 (complexity ledger), §16 (exact caches).

**Status.** S1 adds `checked.h` (overflow-checked `size_t` multiplication and addition used before every allocation, spec §11.1) and `refcount.h` (atomic handle reference counts, spec §17 sharing; the count is reached through a pointer so const handles can be retained without a cast). S2 (review items 6 and 7) adds `canon_u64_add`/`canon_u64_mul` to `checked.h` (exact counts, multiplicities and stream lengths) and `alloc.h` with `canon_alloc_array`, the one checked array allocator (max(count, 1) elements; size overflow is `CAPACITY_LIMIT`, a failed `malloc` `RESOURCE_LIMIT`), used by the graph, root-image, refinement-scratch and search-state allocations. The admission planner, live-memory ledger and workspace storage are slices **S8**/**M5**.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
