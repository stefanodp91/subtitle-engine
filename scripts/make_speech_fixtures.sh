#!/usr/bin/env bash
# The test videos with a voice, made on the spot with macOS's speech synthesis
# (`say`) into fixtures/speech/, which git ignores: the voice is not shipped
# with the engine. macOS only; needs ffmpeg.
#   english.mkv  about 70 s: English speech, 30 s of a plain tone, English speech (AAC)
#   italian.mkv  about 20 s of Italian speech (AAC)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/fixtures/speech"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"
cat > "$TMP/en_a.txt" <<'TXT'
I was waiting at the station for the last train. The clock above the platform had stopped at ten past nine, and nobody seemed to care. A woman with a red umbrella sat on the bench next to me and read a newspaper from the day before. When the train finally arrived, it was almost empty. I took a seat by the window and watched the dark fields go by. The conductor checked my ticket twice and smiled.
TXT
cat > "$TMP/en_b.txt" <<'TXT'
The next morning the town was covered in snow. Children built a snowman in front of the bakery, and the baker gave them warm bread for free. By noon the sun came out and the streets turned to water. Everybody went back to work as if nothing had happened. In the evening the snow came back, softer this time, and the lights of the square stayed on until midnight. Nobody wanted to go home.
TXT
cat > "$TMP/it.txt" <<'TXT'
Ieri sera sono andato al mercato con mia sorella. Abbiamo comprato pane, formaggio e una cesta di pomodori. Poi siamo tornati a casa a piedi, perché la sera era calda e il cielo era pieno di stelle. Lungo la strada abbiamo incontrato un vecchio amico di scuola, che ci ha raccontato del suo viaggio in montagna e del lago ghiacciato dove ha pescato per tutta la settimana.
TXT
say -v Samantha -o "$TMP/en_a.aiff" -f "$TMP/en_a.txt"
say -v Samantha -o "$TMP/en_b.aiff" -f "$TMP/en_b.txt"
say -v Alice -o "$TMP/it.aiff" -f "$TMP/it.txt"
for f in en_a en_b it; do ffmpeg -v error -y -i "$TMP/$f.aiff" -ar 48000 -ac 2 "$TMP/$f.wav"; done
ffmpeg -v error -y -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=30" -ac 2 "$TMP/tone.wav"
ffmpeg -v error -y -i "$TMP/en_a.wav" -i "$TMP/tone.wav" -i "$TMP/en_b.wav" \
  -filter_complex "[0:a][1:a][2:a]concat=n=3:v=0:a=1" -c:a aac -b:a 64k -metadata:s:a:0 language=eng "$OUT/english.mkv"
ffmpeg -v error -y -i "$TMP/it.wav" -c:a aac -b:a 64k -metadata:s:a:0 language=ita "$OUT/italian.mkv"
for f in english italian; do
  echo "$f.mkv: $(ffprobe -v error -show_entries format=duration -of csv=p=0 "$OUT/$f.mkv") s"
done
echo "tone from $(ffprobe -v error -show_entries format=duration -of csv=p=0 "$TMP/en_a.wav") s"
