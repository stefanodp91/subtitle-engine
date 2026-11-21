#pragma once
// Heard cues joined into whole sentences (one sentence often spans two cues,
// and a cue holds the end of one and the start of the next), each with the
// time it is said: the time of a cue spread over its characters.

#include <vector>

#include "subtitle_engine/cue.hpp"
#include "subtitle_engine/cue_rules.hpp"

namespace subtitle_engine {

std::vector<Sentence> joinSentences(const std::vector<Cue>& cues, const JoinRules& rules);

}  // namespace subtitle_engine
