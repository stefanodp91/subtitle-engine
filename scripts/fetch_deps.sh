#!/usr/bin/env bash
# Downloads the pinned dependencies into third_party/ and applies patches/.
# Every git ref and every file hash comes from scripts/deps.lock.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
LOCK="$ROOT/scripts/deps.lock"
mkdir -p "$TP/_dl"

field() { grep -v '^#' "$LOCK" | awk -F'|' -v n="$1" -v k="$2" '$1==n {print $k}'; }

check_sha() {
  local file="$1" want="$2" got
  got="$(shasum -a 256 "$file" | cut -d' ' -f1)"
  [[ "$got" == "$want" ]] || { echo "hash mismatch for $file: $got" >&2; exit 1; }
}

fetch_file() {
  local name="$1" out="$2" url sha
  url="$(field "$name" 3)"; sha="$(field "$name" 4)"
  [[ -f "$out" ]] || curl -sSfL -o "$out" "$url"
  check_sha "$out" "$sha"
}

# Mozilla's inference engine: sparse checkout of inference/ at the pinned commit.
MT="$TP/mozilla-translations"
MT_REF="$(field mozilla-translations 4)"
if [[ ! -d "$MT/.git" ]]; then
  git clone -q --filter=blob:none --sparse --no-checkout "$(field mozilla-translations 3)" "$MT"
  git -C "$MT" sparse-checkout set inference
  git -C "$MT" checkout -q "$MT_REF"
fi
[[ "$(git -C "$MT" rev-parse HEAD)" == "$MT_REF" ]] || { echo "mozilla-translations is not at $MT_REF" >&2; exit 1; }

grep -v '^#' "$LOCK" | awk -F'|' '$1=="submodule" {print $2"|"$3}' | while IFS='|' read -r path sha; do
  git -C "$MT" submodule update -q --init --depth 1 "$path"
  [[ "$(git -C "$MT/$path" rev-parse HEAD)" == "$sha" ]] || { echo "$path is not at $sha" >&2; exit 1; }
done
RUY="$MT/inference/marian-fork/src/3rd_party/ruy"
CPUINFO_SHA="$(field ruy-submodule 3)"
git -C "$RUY" submodule update -q --init --depth 1 third_party/cpuinfo
[[ "$(git -C "$RUY/third_party/cpuinfo" rev-parse HEAD)" == "$CPUINFO_SHA" ]] || { echo "cpuinfo is not at $CPUINFO_SHA" >&2; exit 1; }

# Patches, applied once (a marker file per patch).
for patch in "$ROOT"/patches/*.patch; do
  marker="$MT/.applied-$(basename "$patch")"
  [[ -f "$marker" ]] && continue
  patch -s -p1 -d "$MT" < "$patch"
  touch "$marker"
done

fetch_file googletest "$TP/_dl/googletest-1.15.2.tar.gz"
if [[ ! -d "$TP/googletest" ]]; then
  mkdir -p "$TP/googletest"
  tar -xzf "$TP/_dl/googletest-1.15.2.tar.gz" -C "$TP/googletest" --strip-components 1
fi
mkdir -p "$TP/nlohmann/include/nlohmann"
fetch_file nlohmann-json "$TP/nlohmann/include/nlohmann/json.hpp"
fetch_file pcre2 "$TP/_dl/pcre2-10.39.tar.gz"
fetch_file zstd "$TP/_dl/zstd-1.5.6.tar.gz"
if [[ ! -d "$TP/zstd" ]]; then
  mkdir -p "$TP/zstd"
  tar -xzf "$TP/_dl/zstd-1.5.6.tar.gz" -C "$TP/zstd" --strip-components 1
fi
echo "dependencies ready in $TP"
