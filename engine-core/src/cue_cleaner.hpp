#pragma once
// The cleaning of heard cues, the rules of the feasibility probe: sounds and
// songs, a text equal to the one before (unless said again after a pause),
// loops of the model, a block heard twice, another subtitle's credits, cues of
// only the prompt's names.

#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

#include "subtitle_engine/cue.hpp"
#include "subtitle_engine/cue_rules.hpp"

namespace subtitle_engine {

class CueCleaner {  // keeps the cues of the last duplicateWindow across calls
 public:
  CueCleaner(CleanupRules rules, std::string promptNames);
  std::vector<Cue> clean(std::vector<Cue> heard);
  void reset();  // after a jump

 private:
  bool isPromptOnly(const std::vector<std::u32string>& words) const;
  bool seenLately(const Cue& cue) const;
  std::string trimLoops(const std::string& text) const;

  CleanupRules rules_;
  std::unordered_set<std::u32string> prompt_;
  std::deque<std::pair<int64_t, std::string>> recent_;  // start and normal form of the cues kept lately
  std::string previous_;                                // the normal form of the last text seen
  int64_t previousEnd_ = -1;
};

}  // namespace subtitle_engine
