#!/usr/bin/env bash
# Builds the three test MKVs (one English text track each) from a generated
# 20 s test video and fixtures/short_en.srt (five cues: italics, one on two rows).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
F="$ROOT/fixtures"
BASE="$(mktemp -d)/base.mkv"
ffmpeg -v error -y -f lavfi -i testsrc=duration=20:size=320x240:rate=25 -f lavfi -i sine=frequency=440:duration=20 \
  -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$BASE"
for kind in srt ass webvtt; do
  case $kind in srt) out=subs_srt.mkv;; ass) out=subs_ass.mkv;; webvtt) out=subs_vtt.mkv;; esac
  ffmpeg -v error -y -i "$BASE" -i "$F/short_en.srt" -map 0:v -map 0:a -map 1 -c:v copy -c:a copy -c:s "$kind" \
    -metadata:s:s:0 language=eng -metadata:s:s:0 title=English "$F/$out"
done
# Two subtitle tracks, the first marked as an image codec (S_VOBSUB, null-padded
# in place of S_TEXT/UTF8): the order players number their subtitle tracks in.
ffmpeg -v error -y -i "$BASE" -i "$F/short_en.srt" -i "$F/short_en.srt" -map 0:v -map 0:a -map 1 -map 2 -c:v copy -c:a copy \
  -c:s srt -metadata:s:s:0 language=eng -metadata:s:s:1 language=eng "$F/subs_mixed.mkv"
python3 -I - "$F/subs_mixed.mkv" <<'PY'
import sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
at = data.find(b"S_TEXT/UTF8")
assert at > 0
data[at:at + 11] = b"S_VOBSUB\0\0\0"
open(path, "wb").write(bytes(data))
PY
rm -rf "$(dirname "$BASE")"
echo "fixtures ready"
