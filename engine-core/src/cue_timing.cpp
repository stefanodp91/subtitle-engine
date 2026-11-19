#include "cue_timing.hpp"

#include <algorithm>

namespace subtitle_engine {

Cue alignCue(const Cue& heard, int64_t firstWordMs, int64_t lastWordMs, int64_t audioEndMs, const TimingRules& rules) {
  // BUG-310: no time past the piece's audio, whisper's own included.
  Cue in = heard;
  if (in.endMs > audioEndMs) {
    in.endMs = audioEndMs;
    in.startMs = std::min(in.startMs, audioEndMs - rules.startBeforeEnd.count());
  }
  if (firstWordMs < 0 || firstWordMs >= audioEndMs) return in;
  lastWordMs = std::min(lastWordMs, audioEndMs);
  Cue out = in;
  if (firstWordMs >= in.endMs) {
    out.startMs = firstWordMs;
    out.endMs = std::min(audioEndMs, std::max<int64_t>(lastWordMs + rules.endPad.count(), firstWordMs + rules.movedMin.count()));
    return out;
  }
  out.startMs = std::min<int64_t>(std::max(in.startMs, firstWordMs), in.endMs - rules.startBeforeEnd.count());
  return out;
}

}  // namespace subtitle_engine
