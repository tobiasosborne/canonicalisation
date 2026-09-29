# Primary hardware sources for the performance appendix

Retrieved 29 September 2026. These archived vendor documents support hardware specifications, not measured canonicalisation performance. See [manifest.json](manifest.json) for source/final URLs, retrieval timestamps, byte lengths, response types and SHA-256 hashes. [fetch_sources.py](fetch_sources.py) records a reproducible retrieval list; PDF responses are checked for PDF magic. Text derivatives are navigation aids; raw HTML/PDF files are the archived evidence.

| File | Version/location used | Evidence |
|---|---|---|
| [amd_ryzen_9_9950x.html](amd_ryzen_9_9950x.html) | General Specifications and Connectivity | CPU cores/threads, advertised clocks, aggregate caches, supported memory topology/rate, PCIe version. |
| [kingston_KVR56U46BD8-32.pdf](kingston_KVR56U46BD8-32.pdf) | VALUERAM1675A, p. 1 | Selected 32 GiB DDR5-5600 ×64 DIMM organization and JEDEC CL46 timing. |
| [nvidia_rtx_blackwell_architecture.pdf](nvidia_rtx_blackwell_architecture.pdf) | v1.1; printed pp. 49–51, Table 4 | RTX 5080 SM count, boost clock, local memory interface/rate/capacity/bandwidth and cache sizes. |
| [nvidia_rtx_5080.html](nvidia_rtx_5080.html) | Product specifications | GPU product configuration, PCIe Gen5 support. |
| [nvidia_cuda_best_practices.html](nvidia_cuda_best_practices.html) | CUDA Best Practices Guide 13.4, §12.1.1 Table 5; §§10.1.1–10.1.2 | Instruction-specific maximum throughput, transfer/batching/overlap considerations and required qualifications. |
| [nvidia_compute_capabilities.html](nvidia_compute_capabilities.html) | CC 12.0 row | Identifies RTX 5080 compute capability for instruction-table selection. |
| [pcisig_generations_2025.pdf](pcisig_generations_2025.pdf) | 15 July 2025, slide 7 | PCIe generation signaling rates and 128b/130b encoding; used for ×16 ideal rate arithmetic. |

`amd_zen5_guide_landing.html` and `amd_zen5_epyc_architecture_landing.html` are JavaScript portal responses, **not the requested guide/whitepaper**. Their manifest status is `portal_only_not_requested_document`. They do not substantiate cache hit latencies, integer port throughput or detailed cache topology in the appendix. Those numerical values are explicitly marked planning/calibration assumptions. A stale PCI-SIG URL returned 404 and the Intel MLC page returned 403 in the initial retrieval; neither unavailable response is used as evidence. A relocated official PCI-SIG PDF was then archived successfully.

No calibration benchmark was run. Clock, sustained bandwidth, loaded latency, launch overhead and actual allocated VRAM must be measured or certified on a real configured machine before upgrading conditional numeric models to hardware claims.
