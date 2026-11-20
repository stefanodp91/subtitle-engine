#include "sentence_joiner.hpp"

#include <algorithm>
#include <cmath>

#include "cue_text.hpp"

namespace subtitle_engine {

namespace ct = cue_text;

namespace {

bool endsSentence(char32_t c) { return c == U'.' || c == U'?' || c == U'!' || c == U'…'; }

// The cues of one group as sentences: a sentence ends at a run of spaces
// after a final stop; the last one may have no stop.
void sentencesOf(const std::vector<Cue>& cues, std::vector<Sentence>* out) {
  std::u32string text;
  std::vector<double> at;  // the time of every character
  for (const Cue& c : cues) {
    const std::u32string t = ct::squeeze(ct::decode(c.text));
    const double span = static_cast<double>(c.endMs - c.startMs);
    for (size_t k = 0; k < t.size(); ++k) {
      text += t[k];
      at.push_back(static_cast<double>(c.startMs) + span * static_cast<double>(k) / static_cast<double>(std::max<size_t>(1, t.size())));
    }
    text += U' ';
    at.push_back(static_cast<double>(c.endMs));
  }
  size_t partStart = 0;
  auto emit = [&](size_t from, size_t to) {  // [from, to): a part, maybe with spaces around
    size_t a = from, b = to;
    while (a < b && ct::isSpace(text[a])) ++a;
    while (b > a && ct::isSpace(text[b - 1])) --b;
    if (a == b) return;
    const size_t endIndex = std::min(to, text.size() - 1);
    out->push_back({std::llround(at[a]), std::llround(at[endIndex]), ct::encode(text.substr(a, b - a))});
  };
  for (size_t i = 1; i < text.size(); ++i) {
    if (!ct::isSpace(text[i]) || !endsSentence(text[i - 1])) continue;
    size_t j = i;
    while (j < text.size() && ct::isSpace(text[j])) ++j;
    emit(partStart, i);
    partStart = j;
    i = j;
  }
  emit(partStart, text.size());
}

}  // namespace

std::vector<Sentence> joinSentences(const std::vector<Cue>& cues, const JoinRules& rules) {
  std::vector<Sentence> out;
  std::vector<Cue> group;
  for (const Cue& c : cues) {
    if (!group.empty() && c.startMs - group.back().endMs >= rules.pause.count()) {  // BUG-300
      sentencesOf(group, &out);
      group.clear();
    }
    group.push_back(c);
  }
  if (!group.empty()) sentencesOf(group, &out);
  return out;
}

}  // namespace subtitle_engine
