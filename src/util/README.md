# src/util

**Purpose.** Small shared internal helpers that belong to no single spec §5 module.

**Status.** Added in S1 (review item 5): `sort.h`/`sort.c`, one stable bottom-up merge sort with a callback comparator (`canon_stable_sort`), used by `src/bsgs/explicit.c` (group element table, §7.2 image-array order) and `src/partition/partition.c` (cell members by signature, §7.1). Tests: `tests/c/test_sort.c`.

**What may live here.** C17 sources and internal headers, no third-party code and no dependencies beyond the C standard library. Helpers carry no semantic rules of their own: ordering decisions stay in the callers' comparators, which cite the spec.
