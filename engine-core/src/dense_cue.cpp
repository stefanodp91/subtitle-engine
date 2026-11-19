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
  if (!at) return at;
  // BUG-310: a cue that crosses the point is heard again too, from its start.
  for (bool moved = true; moved;) {
    moved = false;
    for (const auto& h : cues)
      if (h.cue.startMs < *at && h.cue.endMs > *at) {
        at = h.cue.startMs;
        moved = true;
      }
  }
  if (*at < pieceStartMs + minAdvance.count()) return std::nullopt;
  return at;
}

}  // namespace subtitle_engine
