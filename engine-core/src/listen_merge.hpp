#pragma once
// The double listen (DEC-886): two listens of the same audio, cut in different
// places, put together stretch by stretch by whisper's confidence, as merge()
// of probe/double_listen.py in the feasibility probe.

#include <cstdint>
#include <functional>
#include <vector>

#include "subtitle_engine/cue_rules.hpp"
#include "subtitle_engine/speech_recognizer.hpp"

namespace subtitle_engine {

// keep: whether the cleaning keeps any of these cues (on a copy of the cleaner, not its state).
using KeepCheck = std::function<bool(const std::vector<Cue>&)>;

struct ListenStretch {  // cues of the two listens that overlap in time, one after the other
  int64_t startMs = 0, endMs = 0;
  std::vector<HeardCue> first, second;
};

// The stretches, in time order: a new one starts where no cue so far reaches.
std::vector<ListenStretch> listenStretches(const std::vector<HeardCue>& first, const std::vector<HeardCue>& second);

// The cues of one stretch: the listen whose text tokens are surer on average (weighted by
// tokens) wins, the second only by more than margin; a stretch only the second heard from
// fillSure; a winner keep() rejects, or a loop, leaves the stretch to the other listen.
std::vector<HeardCue> mergeStretch(const ListenStretch& stretch, const DoubleListenRules& rules, const KeepCheck& keep);

std::vector<HeardCue> mergeListens(const std::vector<HeardCue>& first, const std::vector<HeardCue>& second,
                                   const DoubleListenRules& rules, const KeepCheck& keep);

}  // namespace subtitle_engine
