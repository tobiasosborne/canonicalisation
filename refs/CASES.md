# Case-file schema for the M0 references

**Purpose.** The JSON input read by ref-a, ref-b and the oracle (`refs/README.md`). Results are written in the seven-field record of `compare/FORMAT.md`; this file covers inputs only.

**Status of this schema.** `compare/compare.py` reads only FORMAT records and never opens a case file, so it fixes nothing here beyond one link: a record's `case_id` must equal the case's `id` (`compare/FORMAT.md`, field table). The one case representation already in the repository is the `p1_cases` section of `vectors/golden.json`. **Part A documents that representation exactly; it is the schema.** Part B lists the further fields that the detailed plan (WP0.2, `docs/detailed-implementation-plan.md` §3) requires for the other objectives but that no existing file yet contains. Part B names are provisional until the maintainer confirms them (`BRIEF.md`, Questions 1 and 2). No second schema is defined.

## Part A: the existing schema (`golden.json`, `p1_cases`)

A case file is a UTF-8 JSON object. In `golden.json` the cases are the array `p1_cases`. The other top-level keys (`schema` = `"golden-vectors-1"`, `source`, `note`, `group_payload_cases`, `convention_vectors`, `other_hand_checked`) are not case records. They are hand-checked unit vectors in their own shapes; see `vectors/README.md`.

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
| `profile` | integer | Canonical profile ID; `1` is P1 (spec §7.1, `profile=0x0001`). | yes |
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
- `records`: an array indexed from 0. Each element is an object with an integer `tag` (the spec §4.1 byte tag) and a payload key that depends on the tag. Two payload keys exist in the corpus today: `literal_hex` (tag 2, the literal bytes) and `children` (tag 3, an array of record indices). Keys for the other tags are not yet fixed (Part B).
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

## Part B: fields WP0.2 requires that no file contains yet (provisional)

WP0.2 lists these inputs. They are written here in Part A's naming style. The maintainer must confirm them before the generated corpus (WP0.6) is written. Until then a reference should accept them under these names and return `INVALID_INPUT` or `UNSUPPORTED_ACTION`, as spec §3.2/§17 require, for anything it cannot interpret.

| Key | Type | Meaning | Source |
|---|---|---|---|
| `character` | array of `+1`/`-1`, one per entry of `group_generators` | χ on the generators, for `SIGNED_CANONICAL_IMAGE` (spec §8.4). Absent means not a signed case. | WP0.2 |
| `order` | string | The named order for `LEX_MIN_IMAGE` (spec §4.3–4.4): `CDAG-BYTE-1` or `SIMPLE-UPPER-1`. | WP0.2 (values from the spec's order names) |
| `labeling` | object `{"rho": image array}` | ρ : Ω → D_n for `CANONICAL_LABELING_COSET` (spec §3.1). | WP0.2 |
| `witness_mode` | `"any"` or `"deterministic"` | Whether the spec §3 deterministic witness is requested. Absent means `"any"`. | WP0.2 |
| `capacity` | object, at least `{"max_search_nodes": int}` | The spec §11.1 logical work quota. | WP0.2 |
| `target` | object of the same kinds as `object` | y for `TRANSPORTER_ONE` and `TRANSPORTER_COSET` (spec §3). | **Not in WP0.2**; needed (BRIEF Question 2) |
| object kind `"tuple"` | `{"kind": "tuple", "atoms": [ints]}`, ordered, repeats allowed | Atom tuples (spec §2.1, first scalar delivery). | WP0.2 lists the kind; the key is provisional |
| DAG payload keys for tags 1, 4–8 | for example `atom`, `children`, `pairs`, ... | Records other than literal and tuple. | not fixed (BRIEF Question 2) |

`golden.json`'s `other_hand_checked.signed` entries use `generators`, `character_of_generator` and top-level `atoms`. They are hand-checked vectors, not case records, and they do not define the signed case schema.
