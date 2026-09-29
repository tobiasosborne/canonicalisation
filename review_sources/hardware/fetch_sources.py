"""Retrieve the primary documents used by the performance appendix.

Downloaded documents are evidence, not measurements. Keep provenance and hashes.
"""
import datetime
import hashlib
import json
import pathlib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent
SOURCES = [
    ("amd_ryzen_9_9950x.html", "https://www.amd.com/en/products/processors/desktops/ryzen/9000-series/amd-ryzen-9-9950x.html"),
    ("amd_zen5_guide_landing.html", "https://docs.amd.com/v/u/en-US/58455_1.00"),
    ("nvidia_rtx_blackwell_architecture.pdf", "https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf"),
    ("nvidia_rtx_5080.html", "https://www.nvidia.com/en-us/geforce/graphics-cards/50-series/rtx-5080/"),
    ("nvidia_cuda_best_practices.html", "https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html"),
    ("nvidia_compute_capabilities.html", "https://developer.nvidia.com/cuda/gpus"),
    ("kingston_KVR56U46BD8-32.pdf", "https://www.kingston.com/datasheets/KVR56U46BD8-32.pdf"),
    ("pcisig_generations_2025.pdf", "https://pcisig.com/sites/default/files/2026-01/PCI-SIG%20PCIe%207.0%20Webinar_Rev5_FINAL.pdf"),
    ("amd_zen5_epyc_architecture_landing.html", "https://www.amd.com/content/dam/amd/en/documents/epyc-business-docs/white-papers/5th-gen-amd-epyc-processor-architecture-white-paper.pdf"),
]

rows = []
for filename, url in SOURCES:
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=45) as response:
            data = response.read()
            final_url = response.url
            content_type = response.headers.get("Content-Type")
        if filename.endswith(".pdf") and not data.startswith(b"%PDF-"):
            raise ValueError("URL returned a non-PDF response; it is not the requested document")
        (ROOT / filename).write_bytes(data)
        row = {"path": filename, "url": url, "final_url": final_url,
               "retrieved_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
               "content_type": content_type, "status": "downloaded"}
        print(filename, len(data), flush=True)
    except Exception as exc:
        row = {"path": filename, "url": url, "status": "failed", "error": str(exc)}
        print(filename, "FAILED", str(exc), flush=True)
    rows.append(row)

(ROOT / "manifest.json").write_text(json.dumps(rows, indent=2) + "\n")
