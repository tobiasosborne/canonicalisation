# src/partition

**Purpose.** Ordered partitions, cell splitting and individualisation with rollback.

**Governing spec sections** (`docs/specification.md`, normative): §7.1 (exact scalar rules for P1), §10 (refinement implementation and termination), §11 (rollback).

**Status.** Implemented in S1: `partition.h`/`partition.c`, flat `lab`/`pos`/`cell_of`/cell starts in semantic order, arrays sized to a capacity with `canon_partition_set_degree` for smaller degrees, `split` by `uint32` signatures (via the shared stable sort of `src/util/`) (§7.1: classes in increasing signature order in the old cell's place, old cells never reordered), individualisation `[{a}, C \ {a}]`, and rollback by whole snapshots (O(n) words per depth; the layout is private, read through `canon_partition_snapshot_lab`). The trail/replay variants of §11.2 are slice **S8**. Tests: `tests/c/test_partition.c`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
