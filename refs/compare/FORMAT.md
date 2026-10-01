# Interchange format for blind-reference comparison

Each implementation writes a UTF-8 text file with **one record per case**, one line per record, fields separated by a single TAB (`\t`), in this order:

```
case_id<TAB>objective<TAB>status<TAB>trace_hex<TAB>bytes_hex<TAB>witness<TAB>group_hex
```

Seven fields.  The seventh, `group_hex`, was added with slice S4 (detailed plan WP0.2), so that group and coset answers are compared as canonical bytes rather than as generator lists.

| Field | Content |
|---|---|
| `case_id` | Identifier of the case; equals the `id` in the input JSON (`refs/vectors/golden.json` or a generated case file). Unique within a file. |
| `objective` | The objective tag as exactly four lowercase hex digits (spec §3 table), e.g. `0001` for `CANONICAL_IMAGE`, `0007` for `SIGNED_CANONICAL_IMAGE`. |
| `status` | One of `COMPLETE`, `CANCELLED`, `CAPACITY_LIMIT`, `RESOURCE_LIMIT`, `INVALID_INPUT`, `UNSUPPORTED_ACTION`, `OUTPUT_ERROR`, `INTERNAL_ERROR` (spec §3.2). |
| `trace_hex` | The complete P1 trace bytes of spec §7.2, lowercase hex, no spaces; empty string if the objective has no trace. |
| `bytes_hex` | The canonical object bytes (CDAG-2 stream, spec §4.1), lowercase hex, no spaces: the canonical image, or for `LEX_MIN_IMAGE` the minimum image's stream under either order (spec §4.4: "return the selected graph in CDAG-2 plus its order key"). For a certified signed zero the payload is `00` (spec §4.3). Empty string if none (transporter, stabiliser and coset objectives). |
| `witness` | The witness as comma-separated decimal images `w[0],w[1],...` (array convention `p[v]=v^p`): the deterministic witness of a canonical image, the `g` attaining a minimum, or the `g` of a transporter hit (`TRANSPORTER_ONE`, nonempty `TRANSPORTER_COSET`); `-` if no witness is reported (stabiliser, an exhausted empty transporter or coset). For n = 0 the witness is the empty permutation and is written `-`. For signed cases implementations append `;sign=+1` or `;sign=-1` to this field. |
| `group_hex` | Lowercase hex, no spaces: the canonical `Group(H)` bytes of spec §9.4 for `STABILISER` (H = A); `Group(H) \|\| Perm(r₀)` for a nonempty `TRANSPORTER_COSET` (H = A, r₀ the least element of A g; spec §9.4) and, from slice S6, for `CANONICAL_LABELING_COSET`; the `SIMPLE-UPPER-1` key (spec §4.4) for `LEX_MIN_IMAGE` under that order; `-` otherwise (including every non-complete status). |

Rules:
- Hex is lowercase with no spaces or `0x` prefix; empty fields are empty strings (two adjacent TABs), never omitted.
- Lines beginning with `#` and blank lines are ignored. No other fields, no trailing whitespace.
- Case **inputs** are the JSON files under `refs/vectors/`; this format carries results only.
- Records are compared field by field after `case_id` lookup, so line order is irrelevant.
- Capacity-limited or otherwise non-complete results still emit a record: the `status` field carries the outcome, `bytes_hex` must be empty unless the bytes are a complete canonical encoding (spec §3.2: incomplete bytes are not a key), and `group_hex` is `-` unless the group or coset is verified complete.

`compare.py` reads two or more such files and reports `AGREE`, `DISAGREE` (with the differing fields) or `MISSING` for each case, exiting 1 if anything is not `AGREE`.  A line with a number of fields other than seven is a format error (exit 2).
