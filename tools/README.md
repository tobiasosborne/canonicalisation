# tools/

**Purpose.** Small standard-library-only developer tools. No dependencies, no network.

**`hexdump_stream.py`.** Strict parser and pretty-printer for `CDAG-2` streams (spec §4.1): header (`43 4e 02`, schema, action, `n`), `q`, records, root; prints each record with its byte offset and rejects trailing bytes. Usage: `python3 tools/hexdump_stream.py 434e0200...` (spaces allowed), `-f FILE` (hex text, `-` for stdin) or `--raw FILE`.

Coverage: tags `01`-`05` and `09` are parsed and checked strictly (atoms in domain, child references below the parent index, sets/multisets increasing, positive multiplicities, shortest `Nat`, sorted arcs, reachability, root is last). Tags `06`, `07`, `08` and `0a` are also parsed: `Perm` per §4.1 (increasing sources, no fixed pairs, bijection on the support), `Group` per §9.4 (both modes, structure only: no group-theoretic validation of the generated group), and named relation records per §4.1. It does not re-derive the canonical DAG numbering of §4.2 and is not a certificate checker.

**Tests.** `tests/python/test_hexdump_stream.py` (the six §7.4 streams plus malformed variants).

**`canon-cli.c`** (slice S1, built as `build/make/canon-cli` by `make`, `canon-cli` by CMake). Links `libcanon` through the public header only. `canon-cli p1-subset --n N --gens "a0,a1,...;b0,..." --atoms "x,y,..." [--max-nodes K] [--id CASE]` solves `CANONICAL_IMAGE` under P1 and prints one `refs/compare/FORMAT.md` record (`CASE`, `0001`, status, trace hex, bytes hex, witness or `-`). `--gens ""` is the trivial group and `--atoms ""` the empty subset; `--max-nodes` sets the §11.1 NODE quota (0 = default). Exit 0 on `COMPLETE`, 3 on another status (hex fields empty), 2 on a usage error. Tested by `tests/python/test_e2e.py`.

**Governing spec sections.** §4.1, §9.4; `canon-cli`: §§3.2, 7, 11.1, 17.
