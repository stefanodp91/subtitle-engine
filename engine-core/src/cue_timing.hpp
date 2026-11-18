#pragma once
// BUG-289: a cue starts at its first word, by the alignment heads (variant D2
// of the feasibility probe).

#include <cstdint>

#include "subtitle_engine/cue.hpp"
#include "subtitle_engine/cue_rules.hpp"

namespace subtitle_engine {

// firstWordMs, lastWordMs: the alignment times of the cue's first and last text token; -1: none.
// With the first word after the cue's end (whisper's times lost, as in a loop) the cue moves
// whole to [first, max(last + endPad, first + movedMin)]; else it starts at the first word,
// never before whisper's start nor closer than startBeforeEnd to its end.
Cue alignCue(const Cue& heard, int64_t firstWordMs, int64_t lastWordMs, const TimingRules& rules);

}  // namespace subtitle_engine
