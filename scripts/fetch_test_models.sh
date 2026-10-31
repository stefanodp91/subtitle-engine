#!/usr/bin/env bash
# Downloads the test translators (Firefox base-memory) into fixtures/models/<pair>/,
# checking the hash of the compressed and of the decompressed file.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 -I - "$ROOT/scripts/test_models.json" "$ROOT/fixtures/models" <<'PY'
import hashlib, json, os, subprocess, sys, urllib.request
spec, dest = json.load(open(sys.argv[1])), sys.argv[2]
sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()
for pair, files in spec.items():
    os.makedirs(os.path.join(dest, pair), exist_ok=True)
    for kind, f in files.items():
        out = os.path.join(dest, pair, f["file"])
        if os.path.exists(out) and sha(out) == f["decompressed_sha256"]:
            continue
        zst = out + ".zst"
        urllib.request.urlretrieve(f["url"], zst)
        if sha(zst) != f["sha256"]:
            sys.exit(f"hash mismatch: {zst}")
        subprocess.run(["zstd", "-d", "-q", "-f", "--rm", zst], check=True)
        if sha(out) != f["decompressed_sha256"]:
            sys.exit(f"hash mismatch: {out}")
print("test models ready in", dest)
PY
