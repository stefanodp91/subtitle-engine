#!/usr/bin/env bash
# Downloads the test translators (Firefox base-memory) into fixtures/models/<pair>/,
# checking the hash of the compressed and of the decompressed file, and the test
# speech models (whisper base, Silero VAD) into fixtures/whisper/ (it may be a
# link to a folder that already holds them).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 -I - "$ROOT/scripts/test_models.json" "$ROOT/fixtures/models" <<'PY'
import hashlib, json, os, subprocess, sys, urllib.request
spec, dest = json.load(open(sys.argv[1])), sys.argv[2]
sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()
for pair, files in spec.items():
    folder = os.path.join(os.path.dirname(dest), "whisper") if pair == "whisper" else os.path.join(dest, pair)
    os.makedirs(folder, exist_ok=True)
    for kind, f in files.items():
        out = os.path.join(folder, f["file"])
        if "decompressed_sha256" not in f:  # a plain file
            if not (os.path.exists(out) and sha(out) == f["sha256"]):
                urllib.request.urlretrieve(f["url"], out + ".part")
                if sha(out + ".part") != f["sha256"]:
                    sys.exit(f"hash mismatch: {out}.part")
                os.replace(out + ".part", out)
            continue
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
