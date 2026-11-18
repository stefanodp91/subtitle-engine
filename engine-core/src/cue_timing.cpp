#include "cue_timing.hpp"

#include <algorithm>

namespace subtitle_engine {

Cue alignCue(const Cue& heard, int64_t firstWordMs, int64_t lastWordMs, const TimingRules& rules) {
  if (firstWordMs < 0) return heard;
  Cue out = heard;
  if (firstWordMs >= heard.endMs) {
    out.startMs = firstWordMs;
    out.endMs = std::max<int64_t>(lastWordMs + rules.endPad.count(), firstWordMs + rules.movedMin.count());
    return out;
  }
  out.startMs = std::min<int64_t>(std::max(heard.startMs, firstWordMs), heard.endMs - rules.startBeforeEnd.count());
  return out;
}

}  // namespace subtitle_engine
