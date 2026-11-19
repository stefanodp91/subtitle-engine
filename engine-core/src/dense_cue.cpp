#include "dense_cue.hpp"

#include <algorithm>

#include "cue_text.hpp"
#include "listen_merge.hpp"

namespace subtitle_engine {

std::optional<int64_t> rehearFrom(const std::vector<HeardCue>& cues, int64_t pieceStartMs, const RehearRules& rules,
                                  std::chrono::milliseconds minAdvance, double loopRatio) {
  std::optional<int64_t> at;
  for (const auto& h : cues) {
    const Cue& c = h.cue;
    if (c.startMs < pieceStartMs + minAdvance.count() || (at && c.startMs >= *at)) continue;
    const auto chars = static_cast<int64_t>(cue_text::length(c.text));
    const int64_t duration = std::max<int64_t>(1, c.endMs - c.startMs);
    if (chars < rules.denseMinChars || chars * 1000 <= rules.denseCharsPerSecond * duration) continue;
    if (loopingText(c.text, loopRatio)) continue;
    at = c.startMs;
  }
  return at;
}

}  // namespace subtitle_engine
