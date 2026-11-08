# Fix lane R2-ledger (B-kernel-02, -07, -08)

Branch: `t3/ledger-decoder-arena-fixes`. Base includes K1 merge `26b28ce`.

## B-kernel-02 — §11.2 ledger counts payload only

- Confirmed by reading `src/arena/mem.c` (`allocate`/`realloc`/`free` charged only
  `bytes`) and by a probe linked against the pre-fix library: 64 blocks of 1 byte
  raised `live_bytes` by exactly 64; a 0-byte block raised it by 0.
  Spec §11.2 (docs/specification.md:373-383): "At every phase account for:
  `M_live = ... + M_allocator_overhead`" and "Reclamation lag counts".
- Change: (in progress)

## B-kernel-07 — decoder O(n) bit maps up front

- Confirmation: pending.

## B-kernel-08 — `canon_dag_normalise` trusts payloads

- Confirmation: pending.

## Tests run

- Pre-fix probe `/tmp/probe_ledger.c` (not committed): `delta=64`, `zero_delta=0`.

## Left undone

- All three fixes.
