#pragma once
// The shape of cues on screen, the rules of the feasibility probe: at most
// maxLines lines of lineChars characters (a long text cut at a sentence end,
// else at a comma, else at the word nearest the middle, its time shared by the
// characters), on screen from minOnScreen to maxOnScreen, gap between two cues,
// two balanced lines, and never over the cue already given out.

#include <optional>
#include <string>
#include <vector>

#include "subtitle_engine/cue.hpp"
#include "subtitle_engine/cue_rules.hpp"

namespace subtitle_engine {

class CueFormatter {
 public:
  explicit CueFormatter(FormatRules rules);
  // previous: the last cue already given out, which cannot change any more.
  std::vector<Cue> format(const std::optional<Cue>& previous, const std::vector<Sentence>& sentences) const;

 private:
  std::vector<std::u32string> split(const std::u32string& text) const;
  std::u32string wrap(const std::u32string& text) const;

  FormatRules rules_;
};

}  // namespace subtitle_engine
