# ref-b: spec questions raised (after the maintainer answers of 2 October 2026 and spec v2.2)

Each entry quotes the sentence, states the readings, and names the reading ref-b implements.
None was resolved by looking at anything outside the blind sandbox.

**Q1. Quota boundary (spec §11.1).** "Exceeding that policy yields `CAPACITY_LIMIT`" and
"A logical work quota ... counts the nodes of the traversal". Reading A: a solve whose total
count is *greater than* `max_search_nodes` is `CAPACITY_LIMIT`; a count equal to the quota
completes. Reading B: reaching the quota already fails. **Implemented: A** (count > quota).

**Q2. Unit of a §8.1 "enumerator visit" (spec §11.1, §8.1).** "one unit per P1 `NODE` token
and one per §8.1 enumerator visit". Reading A: every call of `visit(H, r)` (internal regions
and leaves) is one unit, so a complete enumeration of G costs Σ_{k=0..m} Π_{i≤k} r_i over the
nontrivial orbit sizes r_i of the base-0..n−1 chain (e.g. 1+4+12+24 = 41 for Sym(4)).
Reading B: only leaves (`consume(r)` calls, |G| in total) count. **Implemented: A.**

**Q3. Is a discrete root counted (spec §11.1, §7.3)?** "the §7.3 root shortcuts are part of
profile P1's policy and visit no tree nodes". §7.3 names two root shortcuts: "A discrete root
returns its one leaf after the required final sweep" and the singleton orbit ("If every
generator fixes x"). Reading A: both shortcuts count zero P1 nodes, so a discrete root costs 0
although the trace still carries its `NODE(0)` token. Reading B: only the singleton-orbit
route counts zero; a discrete root is one `NODE` token, one unit. **Implemented: A** (the P1
root is charged only when it branches; the singleton-orbit route charges no P1 node at all).
Only quota-boundary cases can tell the readings apart: runs of T1, T2 and the first 718 T3
cases under reading B gave records identical to the final runs under reading A.

**Q4. Unreachable DAG input records (spec §4.2).** "Validate acyclicity with a bounded
explicit traversal before interning. Source sharing, unreachable allocation records and
insertion order have no meaning. Discard unreachable input nodes". Reading A: an unreachable
record that is malformed (bad tag, atom out of range, cycle among unreachable records) still
makes the input `INVALID_INPUT`. Reading B: unreachable records are discarded unexamined.
**Implemented: A** (every supplied record is parsed and range-checked, and acyclicity is
checked over all records). No corpus case has an unreachable record that is malformed.

**Q5. Fields of a non-`COMPLETE` record (FORMAT.md).** FORMAT fixes `bytes_hex` (empty
unless complete) and `group_hex` (`-` unless verified complete), but not `trace_hex` or
`witness` for `CAPACITY_LIMIT`, `INVALID_INPUT` or `UNSUPPORTED_ACTION`. **Implemented:**
empty trace, witness `-` (no `;sign=` suffix, also for objective 7).

**Q6. `SIMPLE-UPPER-1` on an ineligible object (spec §4.4).** "`SIMPLE-UPPER-1` (order
`0x0002`) is available only for uncoloured simple undirected graphs". Reading A:
`UNSUPPORTED_ACTION` (a recognised order that is unavailable for this input, §2.1).
Reading B: `INVALID_INPUT`. **Implemented: A.** Every corpus case with this order is
eligible (empty graphs on n ≤ 1 in T1/T2).

**Q7. Which roots are "top-level subsets" and "top-level graphs" (spec §7.1)?** "On the
top-level subset (a set consisting only of atom nodes), the initial key of a is membership
0/1; on a top-level graph it is B(vertex_colour[a]); on every other root it is the empty key."
Reading A: decided by the normalised value (a `dag` object whose root record is a set of atom
nodes is a subset; a `dag` whose root is a tag-09 record is a graph), consistent with §5
("must factor through decode"). Reading B: decided by the input `kind`. **Implemented: A.**
This also decides transporter equality across kinds: a `subset` and a `dag` with the same
normalised value are the same object (equal CDAG-2 streams).

**Q8. Layout of the graph O-stage signature (spec §7.1).** "sig(v) consists of exact counts,
in order: for arc labels sorted by B(label), then cell index j: outgoing multiplicity from v
into C[j], incoming multiplicity from C[j] into v". Reading A (implemented): nested loops,
label outermost, then j, and for each (label, j) the pair (out, in), i.e.
(out(ℓ0,C0), in(ℓ0,C0), out(ℓ0,C1), in(ℓ0,C1), ..., out(ℓ1,C0), ...). Reading B: for each label,
all outgoing counts over j, then all incoming counts over j. The split class order (hence
traces) can differ between the readings. The §7.4 arc case (one label, one cell) cannot tell
them apart. Which labels enter (all labels of the graph, or those at v) does not matter: an
all-zero block does not change a lexicographic comparison of equal-length vectors.

**Q9. Status precedence.** When a case is both unsupported and invalid (for example a
signed labeling problem whose generators are not bijections), which status wins?
**Implemented:** objective 8/9 and the signed labeling problem return `UNSUPPORTED_ACTION`
before any other key is read; then `encoding` ≠ 2 and a `profile` other than the CASES.md
objective table value give `UNSUPPORTED_ACTION`; then `n`, generators, `witness_mode`,
`capacity` (a `work_policy` other than 1 is `UNSUPPORTED_ACTION`) and the object are
validated (`INVALID_INPUT`); then objective keys (`order`, `target`, `labeling`, `character`).

**Q10. Unsigned objectives on the singleton-orbit route (spec §11.1, §3).** Following
§11.1, when every generator fixes x the P1 part of `CANONICAL_IMAGE` and
`CANONICAL_LABELING_COSET` counts zero nodes, but (spec §3) the deterministic-mode stabiliser
enumeration of x is still run and counted in full ("On the signed §7.3 singleton-orbit route
A=G is known without traversal and nothing is enumerated" is stated for the signed objective
only). **Implemented as stated**; the trace reported is the full unpruned §7.2 trace.

**Q11. Character evaluation and the lifted group.** Not a question of reading, but recorded:
ref-b validates χ by |G̃| = |G| for the lifted group on Ω ⊔ {+,−} (points n, n+1), and runs
the §8.4 stabiliser enumeration over G̃ with base 0..n+1; since the kernel is trivial, its
visits are exactly the lifts of the §8.1 enumeration of G (same orbits, same least
transporters, same count), and χ(r) is read off the marker points.
