# Seals

Each completed reference implementation records the commit hash that contains it. Rows are append-only: a change after sealing needs a new row, never an edit. See `README.md` for the protocol.

`Toolchain` is the exact compiler or runtime version used (for ref-b, the Julia version). `Blind w.r.t.` lists the paths the author had not read before sealing; a reference that is not blind with respect to some path says so (ref-a is not blind with respect to `checks/` and `tools/`).

| Implementation | Language | Toolchain | Author | Blind w.r.t. | Sealed commit | Date |
|---|---|---|---|---|---|---|
