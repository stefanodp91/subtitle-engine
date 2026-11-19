#pragma once
// Where a piece of audio ends, the rule of the feasibility probe: the pieces
// after the start or a jump grow from firstPiece to piece; the first one closes
// at its first pause after firstCut; the others at their last pause after
// min(cutMin, piece / 2), or at their full length.

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "subtitle_engine/cue_rules.hpp"
#include "subtitle_engine/speech_recognizer.hpp"

namespace subtitle_engine {

// Where the piece ends, in ms from the start of the buffer; nullopt: wait for more audio.
// speech: what the detector heard in the buffer; pieceSinceRamp: 0 for the first piece
// after the start, a jump or a stall. At the end of the audio the rest is the piece,
// or 0 (dropped) when shorter than minTail.
std::optional<int64_t> cutPoint(const PieceRules& rules, int64_t bufferedMs, const std::vector<SpeechSpan>& speech,
                                int pieceSinceRamp, bool endOfAudio);

// BUG-311: the first piece after the start, a jump or a stall looks for its pause at fixed
// points of its own audio (firstCut + firstPauseOpen, then every firstProbeEvery), each with
// the audio up to that point only, up to bufferedMs and below the piece's length, so its cut
// does not depend on when the audio arrived. speech(untilMs): what the detector hears of the
// piece's audio up to untilMs. lastProbeMs: the last point looked at, -1 for none; updated.
std::optional<int64_t> firstCutPoint(const PieceRules& rules, int64_t bufferedMs, int64_t* lastProbeMs,
                                     const std::function<std::vector<SpeechSpan>(int64_t)>& speech);

}  // namespace subtitle_engine
