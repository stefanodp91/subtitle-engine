#include "listen_merge.hpp"

#include <zlib.h>

#include <algorithm>
#include <optional>

namespace subtitle_engine {

namespace {

std::optional<double> sureOf(const std::vector<HeardCue>& cues) {
  int tokens = 0;
  double sum = 0;
  for (const auto& c : cues) {
    tokens += c.tokens;
    sum += c.sure * c.tokens;
  }
  if (cues.empty()) return std::nullopt;
  return tokens ? sum / tokens : 0.0;
}

// A loop of the model: its text compresses far better than speech does (whisper's own check).
bool looping(const std::vector<HeardCue>& cues, double maxRatio) {
  std::string text;
  for (const auto& c : cues) text += (text.empty() ? "" : " ") + c.cue.text;
  if (text.empty()) return false;
  uLongf size = compressBound(static_cast<uLong>(text.size()));
  std::vector<Bytef> out(size);
  if (compress2(out.data(), &size, reinterpret_cast<const Bytef*>(text.data()), static_cast<uLong>(text.size()),
                Z_DEFAULT_COMPRESSION) != Z_OK || size == 0)
    return false;
  return static_cast<double>(text.size()) / static_cast<double>(size) > maxRatio;
}

bool kept(const std::vector<HeardCue>& cues, const DoubleListenRules& rules, const KeepCheck& keep) {
  if (cues.empty() || looping(cues, rules.loopRatio)) return false;
  std::vector<Cue> plain;
  for (const auto& c : cues) plain.push_back(c.cue);
  return keep(plain);
}

}  // namespace

std::vector<ListenStretch> listenStretches(const std::vector<HeardCue>& first, const std::vector<HeardCue>& second) {
  struct Tagged {
    const HeardCue* cue;
    bool second;
  };
  std::vector<Tagged> all;
  for (const auto& c : first) all.push_back({&c, false});
  for (const auto& c : second) all.push_back({&c, true});
  std::stable_sort(all.begin(), all.end(),
                   [](const Tagged& a, const Tagged& b) { return a.cue->cue.startMs < b.cue->cue.startMs; });
  std::vector<ListenStretch> out;
  int64_t reach = INT64_MIN;
  for (const auto& t : all) {
    if (out.empty() || t.cue->cue.startMs >= reach) {
      out.push_back({t.cue->cue.startMs, t.cue->cue.endMs, {}, {}});
      reach = t.cue->cue.endMs;
    }
    (t.second ? out.back().second : out.back().first).push_back(*t.cue);
    reach = std::max(reach, t.cue->cue.endMs);
    out.back().endMs = reach;
  }
  return out;
}

std::vector<HeardCue> mergeStretch(const ListenStretch& s, const DoubleListenRules& rules, const KeepCheck& keep) {
  const auto a = sureOf(s.first), b = sureOf(s.second);
  const bool secondKept = kept(s.second, rules, keep);
  const bool takeSecond =
      b && secondKept && ((!a && *b >= rules.fillSure) || (a && *b > *a + rules.margin) || (a && !kept(s.first, rules, keep)));
  return takeSecond ? s.second : s.first;
}

std::vector<HeardCue> mergeListens(const std::vector<HeardCue>& first, const std::vector<HeardCue>& second,
                                   const DoubleListenRules& rules, const KeepCheck& keep) {
  std::vector<HeardCue> out;
  for (const auto& s : listenStretches(first, second)) {
    auto cues = mergeStretch(s, rules, keep);
    out.insert(out.end(), cues.begin(), cues.end());
  }
  return out;
}

}  // namespace subtitle_engine
