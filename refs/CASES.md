# Case-file schema for the M0 references

**Purpose.** The JSON input read by ref-a, ref-b and the oracle (`refs/README.md`). Results are written in the seven-field record of `compare/FORMAT.md`; this file covers inputs only.

**Status of this schema.** `compare/compare.py` reads only FORMAT records and never opens a case file, so it fixes nothing here beyond one link: a record's `case_id` must equal the case's `id` (`compare/FORMAT.md`, field table). The one case representation already in the repository is the `p1_cases` section of `vectors/golden.json`. **Part A documents that representation exactly.** Part B defines the further fields that the detailed plan (WP0.2, `docs/detailed-implementation-plan.md` §3) requires for the other objectives. Both parts are definitive: the maintainer fixed them on 2 October 2026 (`BRIEF.md`, Maintainer answers 1 and 2), and WP0.2 is amended to these names. No second schema is defined.

## Part A: the existing schema (`golden.json`, `p1_cases`)

A case file is a UTF-8 JSON object. In `golden.json` the cases are the array `p1_cases`; in a generated case file they are the array `cases` (Part B). The other top-level keys (`schema` = `"golden-vectors-1"`, `source`, `note`, `group_payload_cases`, `convention_vectors`, `other_hand_checked`) are not case records. They are hand-checked unit vectors in their own shapes; see `vectors/README.md`.

Each case is a JSON object with these keys:

| Key | JSON type | Meaning | Blind author may read |
|---|---|---|---|
| `id` | string | Case identifier, unique in the file; copied to FORMAT `case_id`. | yes |
| `source` | string | Provenance (for example `"docs/specification.md §7.4"`). | yes |
| `spec_text` | string | The spec's own wording of the case. | yes |
| `n` | integer ≥ 0 | Degree: atoms are `0..n-1` (spec §2.1, §4.1 `U32(n)`). | yes |
| `group_generators` | array of arrays of integers | Generators of G. Each is an image array of length `n` with `p[v] = v^p` (spec §3). `[]` means the trivial group. | yes |
| `object` | object | The input x; see "Object kinds" below. | yes |
| `objective` | string | Objective name from the spec §3 table without its tag, for example `"CANONICAL_IMAGE"`. | yes |
| `objective_tag` | integer | The same objective's uint16 tag as a decimal integer, for example `1`. FORMAT writes it as four lowercase hex digits (`0001`). | yes |
| `profile` | integer | Canonical profile ID; `1` is P1 (spec §7.1, `profile=0x0001`), `0` is `NO_TREE` (spec §4.3; Part B's objective table). | yes |
| `encoding` | integer | Encoding ID; `2` is CDAG-2 (spec §4.1, `0x0002`). | yes |
| `expected_trace_hex` | string | Expected answer. | **no**, until sealed |
| `expected_stream_hex` | string | Expected answer. | **no**, until sealed |
| `expected_witness` | array of integers | Expected answer. | **no**, until sealed |
| `note` | string | Free text; may be `""`. | yes |

Hex strings are lowercase with no spaces and no `0x` prefix (`vectors/README.md`). A reader must ignore keys it does not know.

### Object kinds

`object.kind` selects the shape. The action on every kind is spec §2.1 `ATOM-TRANSPORT-1`.

**`"subset"`**: `atoms`, an array of distinct integers in `0..n-1`. Order and duplicates carry no meaning; it is a set of atom nodes (spec §2.1).

**`"graph"`**: a coloured directed multigraph on vertices `0..n-1` (spec §4.1, tag `09`).
- `vertex_colours_hex`: an array of exactly `n` hex strings. Entry `v` is the byte colour of vertex `v`; `""` is the empty colour.
- `arcs`: an array of objects `{"source": int, "target": int, "label_hex": hex string, "multiplicity": int}`. Loops and repeated arcs are allowed in the input. How they combine, and why multiplicity 0 is invalid, is defined by spec §4.1, not here.

**`"dag"`**: a nested object given as raw records (spec §4.1, §4.2).
- `records`: an array indexed from 0. Each element is an object with an integer `tag` (the spec §4.1 byte tag) and a payload key that depends on the tag. Two payload keys exist in the corpus today: `literal_hex` (tag 2, the literal bytes) and `children` (tag 3, an array of record indices). Part B defines the keys for every other tag.
- `root`: the index of the root record.
- `note` (optional): free text.
The input records need not be normalised: sharing and duplication are both allowed, and normalisation is the reference's job (spec §4.2).

### Worked examples (taken from `golden.json`, answer fields removed)

Subset:

```json
{"id": "p1-n2-x0-sym2", "source": "docs/specification.md §7.4",
 "spec_text": "n=2, x={0}, G=<[1,0]>",
 "n": 2, "group_generators": [[1,0]],
 "object": {"kind": "subset", "atoms": [0]},
 "objective": "CANONICAL_IMAGE", "objective_tag": 1, "profile": 1, "encoding": 2, "note": ""}
```

Coloured directed multigraph (one unit arc 0→1, all labels and colours empty):

```json
{"id": "p1-n2-arc01-sym2", "n": 2, "group_generators": [[1,0]],
 "object": {"kind": "graph", "vertex_colours_hex": ["", ""],
            "arcs": [{"source": 0, "target": 1, "label_hex": "", "multiplicity": 1}]},
 "objective": "CANONICAL_IMAGE", "objective_tag": 1, "profile": 1, "encoding": 2}
```

Nested DAG (x = (b, b), with b the empty literal stored once and shared):

```json
{"id": "p1-n0-dag-tuple-bb", "n": 0, "group_generators": [],
 "object": {"kind": "dag",
            "records": [{"tag": 2, "literal_hex": ""}, {"tag": 3, "children": [0, 0]}],
            "root": 1},
 "objective": "CANONICAL_IMAGE", "objective_tag": 1, "profile": 1, "encoding": 2}
```

(`source`, `spec_text` and `note` are left out of the last two examples for brevity. They are present in the file.)

## Part B: the remaining case fields (definitive, 2 October 2026)

These fields complete the WP0.2 schema (`docs/detailed-implementation-plan.md` §3, amended to these names). They follow Part A's naming style and were fixed by the maintainer on 2 October 2026 (`BRIEF.md`, Maintainer answers 1–3). A reference returns `INVALID_INPUT` or `UNSUPPORTED_ACTION`, as spec §2.1, §3.2 and §17 require, for anything it cannot interpret; it never guesses.

### Container

A **generated case file** (WP0.6) is a UTF-8 JSON object whose case records are the array `cases`. Any other top-level key (for example a seed or a note) is metadata that runners ignore. `golden.json` keeps its array name `p1_cases` (Part A). A runner accepts either name and processes every record in that array.

### Keys of a case record

Required in every case: `id`, `n`, `group_generators`, `object`, `objective`, `objective_tag`, `profile`, `encoding` (Part A). Optional everywhere: `source`, `spec_text`, `note`. The keys below are required or forbidden by objective, as the objective table states.

| Key | JSON type | Meaning |
|---|---|---|
| `target` | object, in the same schema as `object` (Part A kinds, plus `"tuple"` below) | y for `TRANSPORTER_ONE` and `TRANSPORTER_COSET` (spec §3 table): the result is about g ∈ G with x^g = y. |
| `character` | array of the JSON integers `1` and `-1`, exactly one entry per entry of `group_generators`, in the same order | χ on the generators for `SIGNED_CANONICAL_IMAGE` (spec §8.4). Entry i is χ(g_i). Signs that define no homomorphism are an input the reference must reject as spec §8.4 states. For the trivial group with `group_generators` `[]` it is `[]`. |
| `order` | string, exactly `"CDAG-BYTE-1"` or `"SIMPLE-UPPER-1"` | The named order of `LEX_MIN_IMAGE` (spec §4.3 order `0x0001`, §4.4 order `0x0002`). |
| `labeling` | object with exactly one key `rho`, an image array of length `n` | ρ : Ω → D_n of spec §3.1, written `rho[v]` = the target coordinate of source atom `v`. It must be a bijection. `object` and `group_generators` are on the source domain Ω = `0..n-1`. |
| `witness_mode` | string, exactly `"any"` or `"deterministic"` | Whether the spec §3 deterministic witness is requested. Absent means `"deterministic"`. Every M0 corpus case requests it, and `compare.py` compares the `witness` field on every case (`BRIEF.md`, Answer 3). `"any"` permits any valid witness; no M0 corpus case uses it. For objectives that report no witness, FORMAT writes `-` whatever the mode. |
| `capacity` | object with the keys `max_search_nodes`, a non-negative JSON integer, and optionally `work_policy`, a positive JSON integer | The logical work quota of spec §11.1, in the unit v2.1 states there (one unit per P1 `NODE` token and per §8.1 enumerator visit, one quota per solve), and the work-policy ID naming the traversal counted (spec §11.1 v2.1). Absent `max_search_nodes` means the default quota 2^20; absent `work_policy` means `1`, the unpruned reference traversal, which is the only value M0 cases use. |

All integers in a case file are JSON numbers without fraction or exponent, read exactly (spec §4.1: no floating-point interpretation).

### Objective table

| `objective` | `objective_tag` | `profile` (spec §4.3) | Requires | Absent |
|---|---|---|---|---|
| `CANONICAL_IMAGE` | 1 | 1 (P1) | | `target`, `character`, `order`, `labeling` |
| `LEX_MIN_IMAGE` | 2 | 0 (`NO_TREE`) | `order` | `target`, `character`, `labeling` |
| `TRANSPORTER_ONE` | 3 | 0 | `target` | `character`, `order`, `labeling` |
| `STABILISER` | 4 | 0 | | `target`, `character`, `order`, `labeling` |
| `CANONICAL_LABELING_COSET` | 5 | 1 | `labeling` | `target`, `character`, `order` |
| `TRANSPORTER_COSET` | 6 | 0 | `target` | `character`, `order`, `labeling` |
| `SIGNED_CANONICAL_IMAGE` | 7 | 1 | `character` | `target`, `order` |

A case generator never writes a key the table marks absent. A `SIGNED_CANONICAL_IMAGE` case that also carries `labeling` is a **signed labeling problem**. Spec v2.1 §3.1 and §12 defer its orientation σ_ρ, and M0 references return `UNSUPPORTED_ACTION` for it (`BRIEF.md`, Answer 12). Objectives 8 and 9 (`CONSTRAINT_ONE`, `CONSTRAINT_ENUM`) are outside M0: a reference returns `UNSUPPORTED_ACTION` and reads no further keys.

### Object kind `"tuple"`

`{"kind": "tuple", "points": [ints]}`: an atom tuple (spec §2.1), each entry in `0..n-1`. Order is significant and repeats are allowed; `[]` is the empty tuple.

### DAG records for every tag

Part A defines `records`, `root` and the two payload keys in use today (tags 2 and 3); they are unchanged. Every record carries `tag` as a decimal JSON integer (tag `0a` is `10`) and the payload keys below, named after the spec §4.1 record. Child indices refer to entries of the same `records` array; they need not be smaller than the parent's index, but the references must be acyclic (spec §4.2). Input records need not be normalised (Part A).

| `tag` | Record (spec §4.1, lower case) | Payload keys |
|---|---|---|
| 1 | atom | `atom`: an integer in `0..n-1`. |
| 2 | literal | `literal_hex`: the literal bytes as hex; `""` is the empty literal. |
| 3 | tuple | `children`: an array of record indices; order is significant and repeats are allowed. |
| 4 | set | `children`: an array of record indices; order and repeats carry no meaning (spec §4.2 deduplicates equal children). |
| 5 | multiset | `pairs`: an array of `{"child": record index, "multiplicity": positive integer}`. Order carries no meaning; equal children combine by adding counts (spec §4.2). A multiplicity of 0 is invalid (spec §4.1). |
| 6 | permutation | `perm`: an image array of length `n` (spec §3 convention). |
| 7 | subgroup | `group_generators`: an array of image arrays of length `n` generating H; `[]` is the trivial group. |
| 8 | labeling coset | `group_generators` (H, as for tag 7) and `r`: an image array of length `n`, any element of the coset H r. The reference computes the least element that spec §4.1 and §9.4 encode. |
| 9 | coloured directed multigraph | `vertex_colours_hex` and `arcs`, exactly as for the object kind `"graph"` (Part A). |
| 10 | named relations | `relations`: an array of `{"name_hex": hex string, "arity": integer, "tuples": [{"atoms": [ints, length arity], "multiplicity": positive integer}]}`. Names, arity and combination rules are spec §4.1's. |

Which tags an M0 reference must support is set by the scope of WP0.4 (`BRIEF.md` §2); a recognised tag outside that scope returns `UNSUPPORTED_ACTION` (spec §2.1). The keys are fixed for all ten so that no second schema is needed later.

### Worked examples (inputs only; no answers)

Transporter target (`TRANSPORTER_ONE`, x = {0}, y = {2}, G = C3):

```json
{"id": "ex-transporter-one-c3", "n": 3, "group_generators": [[1,2,0]],
 "object": {"kind": "subset", "atoms": [0]},
 "target": {"kind": "subset", "atoms": [2]},
 "objective": "TRANSPORTER_ONE", "objective_tag": 3, "profile": 0, "encoding": 2,
 "witness_mode": "deterministic", "capacity": {"max_search_nodes": 1048576}}
```

Tuple (`CANONICAL_IMAGE`, x = (2, 0, 2), G = Sym(3)):

```json
{"id": "ex-tuple-sym3", "n": 3, "group_generators": [[1,2,0], [1,0,2]],
 "object": {"kind": "tuple", "points": [2, 0, 2]},
 "objective": "CANONICAL_IMAGE", "objective_tag": 1, "profile": 1, "encoding": 2,
 "witness_mode": "deterministic"}
```

Labeling (the spec §7.4 example Ω = (a, b), ρ = [1, 0], G = 1, x = {a}, with a = 0):

```json
{"id": "ex-labeling-rho", "n": 2, "group_generators": [],
 "object": {"kind": "subset", "atoms": [0]},
 "labeling": {"rho": [1, 0]},
 "objective": "CANONICAL_LABELING_COSET", "objective_tag": 5, "profile": 1, "encoding": 2,
 "witness_mode": "deterministic"}
```

Signed (the spec §7.4 example x = {0}, G = ⟨[1,0]⟩ with χ([1,0]) = −1):

```json
{"id": "ex-signed-x0", "n": 2, "group_generators": [[1,0]], "character": [-1],
 "object": {"kind": "subset", "atoms": [0]},
 "objective": "SIGNED_CANONICAL_IMAGE", "objective_tag": 7, "profile": 1, "encoding": 2,
 "witness_mode": "deterministic"}
```

A generated file holding these records is `{"cases": [ ... ]}`.

**Historical remark.** `golden.json`'s `group_payload_cases` and `other_hand_checked` entries use older key names (`generators`, `character_of_generator`, `G_generators`, top-level `atoms`, `rho`). They are hand-checked unit vectors, not case records, and they do not define this schema. WP0.2's original names `case_id`, `generators` and a hex `objective` were replaced by the Part A names on 2 October 2026; `case_id` remains only as the first FORMAT field.
