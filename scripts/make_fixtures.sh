#!/usr/bin/env bash
# Builds the test videos: the MKVs with text tracks (from a generated 20 s test
# video and fixtures/short_en.srt: five cues, italics, one on two rows) and the
# audio-only ones for the audio reader (no voice).
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
# Audio without a voice for the audio reader: a 440 Hz tone of 20 s with 5 s of
# silence in the middle, in the codecs and containers films come in.
TONE="$(dirname "$BASE")/tone.wav"
ffmpeg -v error -y -f lavfi -i "sine=frequency=440:duration=7.5" -f lavfi -i "anullsrc=r=48000:cl=stereo:d=5" \
  -f lavfi -i "sine=frequency=440:duration=7.5" \
  -filter_complex "[0:a]aformat=sample_rates=48000:channel_layouts=stereo[a];[2:a]aformat=sample_rates=48000:channel_layouts=stereo[c];[a][1:a][c]concat=n=3:v=0:a=1" \
  "$TONE"
A=(-metadata:s:a:0 language=eng)
ffmpeg -v error -y -i "$TONE" -c:a ac3 -b:a 64k "${A[@]}" "$F/audio_ac3.mkv"
ffmpeg -v error -y -i "$TONE" -c:a eac3 -b:a 64k "${A[@]}" "$F/audio_eac3.mkv"
ffmpeg -v error -y -i "$TONE" -ac 1 -ar 8000 -c:a dca -strict -2 -b:a 64k "${A[@]}" "$F/audio_dts.mkv"
ffmpeg -v error -y -i "$TONE" -i "$TONE" -map 0:a -map 1:a -c:a ac3 -b:a 48k \
  -metadata:s:a:0 language=eng -metadata:s:a:1 language=ita -disposition:a:0 default -disposition:a:1 0 "$F/audio_two_tracks.mkv"
# Written to a pipe the Matroska muxer cannot go back: no Cues (no index).
ffmpeg -v error -y -i "$TONE" -c:a ac3 -b:a 64k "${A[@]}" -f matroska pipe:1 > "$F/audio_no_cues.mkv"
# MP4 with its index (moov) at the end, as cameras and some encoders write it.
ffmpeg -v error -y -i "$TONE" -c:a aac -b:a 48k "$F/audio_moov_end.mp4"
ffmpeg -v error -y -i "$TONE" -ac 1 -ar 8000 -c:a alac "${A[@]}" "$F/audio_alac.mkv"
ffmpeg -v error -y -f lavfi -i testsrc=duration=5:size=160x120:rate=10 -c:v libx264 -pix_fmt yuv420p "$F/audio_none.mkv"
rm -rf "$(dirname "$BASE")"
echo "fixtures ready"
