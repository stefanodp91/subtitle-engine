#pragma once
// BUG-307: a cue whisper packed denser than speech at a piece's end.

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "subtitle_engine/cue_rules.hpp"
#include "subtitle_engine/speech_recognizer.hpp"

namespace subtitle_engine {

// Where the listening has to start again in a piece just heard: the start of the
// earliest cue with more than rules.denseCharsPerSecond characters a second (code
// points, at least rules.denseMinChars), at least minAdvance after the piece's start
// so the listening always moves on; none otherwise. A loop (compression ratio above
// loopRatio) is left to the cleaning.
std::optional<int64_t> rehearFrom(const std::vector<HeardCue>& cues, int64_t pieceStartMs, const RehearRules& rules,
                                  std::chrono::milliseconds minAdvance, double loopRatio);

}  // namespace subtitle_engine
