# Interchange format for blind-reference comparison

Each implementation writes a UTF-8 text file with **one record per case**, one line per record, fields separated by a single TAB (`\t`), in this order:

```
case_id<TAB>objective<TAB>status<TAB>trace_hex<TAB>bytes_hex<TAB>witness
```

| Field | Content |
|---|---|
| `case_id` | Identifier of the case; equals the `id` in the input JSON (`refs/vectors/golden.json` or a generated case file). Unique within a file. |
| `objective` | The objective tag as exactly four lowercase hex digits (spec §3 table), e.g. `0001` for `CANONICAL_IMAGE`, `0007` for `SIGNED_CANONICAL_IMAGE`. |
| `status` | One of `COMPLETE`, `CANCELLED`, `CAPACITY_LIMIT`, `RESOURCE_LIMIT`, `INVALID_INPUT`, `UNSUPPORTED_ACTION`, `OUTPUT_ERROR`, `INTERNAL_ERROR` (spec §3.2). |
| `trace_hex` | The complete P1 trace bytes of spec §7.2, lowercase hex, no spaces; empty string if the objective has no trace. |
| `bytes_hex` | The canonical object bytes (CDAG-2 stream, spec §4.1), lowercase hex, no spaces. For a certified signed zero the payload is `00` (spec §4.3). Empty string if none. |
| `witness` | The deterministic witness as comma-separated decimal images `w[0],w[1],...` (array convention `p[v]=v^p`), or `-` if no witness is reported. For n = 0 the witness is the empty permutation and is written `-`. For signed cases implementations append `;sign=+1` or `;sign=-1` to this field. |

Rules:
- Hex is lowercase with no spaces or `0x` prefix; empty fields are empty strings (two adjacent TABs), never omitted.
- Lines beginning with `#` and blank lines are ignored. No other fields, no trailing whitespace.
- Case **inputs** are the JSON files under `refs/vectors/`; this format carries results only.
- Records are compared field by field after `case_id` lookup, so line order is irrelevant.
- Capacity-limited or otherwise non-complete results still emit a record: the `status` field carries the outcome, and `bytes_hex` must be empty unless the bytes are a complete canonical encoding (spec §3.2: incomplete bytes are not a key).

`compare.py` reads two or more such files and reports `AGREE`, `DISAGREE` (with the differing fields) or `MISSING` for each case, exiting 1 if anything is not `AGREE`.
