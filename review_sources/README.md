# Primary sources (provenance only)

The reviews in this repository cite third-party primary material: arXiv papers and their TeX, a scan of Karp (1972), vendor hardware documents, the pinned isocert repository (Isabelle/HOL and C++), and selected pinned mathlib files. Their licences do not permit redistribution under this repository's AGPL licence, so **Git tracks only their provenance**. Under that rule, the local copies are the ground truth for literature claims; web summaries are discovery aids only.

| File | Content |
|---|---|
| [SOURCES.json](SOURCES.json) | Inventory of every cited file: path, bytes, SHA-256 and origin (download URL, member of a verified archive, or locally derived output with its command) |
| [fetch_sources.py](fetch_sources.py) | Verifies local copies against the inventory, or rebuilds them with `--fetch` |
| [algorithms/manifest.json](algorithms/manifest.json), [algorithms/manifest_supplemental.json](algorithms/manifest_supplemental.json) | Original acquisition records for the algorithm literature |
| [formalisation/PROVENANCE.md](formalisation/PROVENANCE.md) | Acquisition, pinning and pruning record for the formalisation sources |
| [hardware/README.md](hardware/README.md), [hardware/manifest.json](hardware/manifest.json) | Hardware documents: which values each supports, retrieval metadata |

## Rebuilding locally

```sh
python3 review_sources/fetch_sources.py            # offline: report OK / MISSING / MISMATCH / CHANGED
python3 review_sources/fetch_sources.py --fetch    # download missing files, extract archive members, verify
python3 review_sources/fetch_sources.py --fetch --only algorithms
```

- **Downloads** are written only if their SHA-256 matches. Vendor and arXiv web pages are marked `hash_stable: false`, because the live page may differ from the retrieved copy. They are fetched and reported as `CHANGED` instead of being rejected, and only the archived copy's hash is evidence for what the reviews read.
- **Archive members** (TeX sources, isocert files, mathlib files) are extracted in one streaming pass from their verified archive and written only if their hash matches.
- **Derived files** (`pdftotext -layout` text and page images of the Karp scan) are not generated automatically. The script prints the recorded command. Regenerated text may differ byte-for-byte across tool versions. The Karp scan has no text layer, so its page images are the readable evidence.

Nothing downloaded is executed, compiled or built. The files land at the paths cited by the reviews, where `.gitignore` keeps them out of Git.
