#pragma once
// What the queue asks of whisper: where there is speech in some audio, and what
// is said in it. The engine's Listener does it; the tests use a fake.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "subtitle_engine/cue.hpp"

namespace subtitle_engine {

struct SpeechSpan {  // from the start of the samples given
  int64_t startMs = 0;
  int64_t endMs = 0;
};

struct HearOptions {
  std::string language;            // whisper code; empty: detect
  std::string prompt;              // the characters' names
  float temperatureInc = 0;        // 0: no second tries
  std::function<bool()> cancelled;
};

struct HeardCue {   // the double listen chooses by whisper's confidence (DEC-886)
  Cue cue;          // in film time, already through alignCue
  double sure = 0;  // mean probability of the cue's text tokens
  int tokens = 0;   // its text tokens (ids below whisper_token_eot)
};

struct Heard {
  std::vector<HeardCue> cues;
  std::string detectedLanguage;  // whisper's guess, or the language asked for
  bool spoken = false;           // some cue has words, not only "(music)" or "♪"
  bool cancelled = false;
};

class SpeechRecognizer {
 public:
  virtual ~SpeechRecognizer() = default;
  virtual std::vector<SpeechSpan> speech(const float* samples, size_t count) = 0;
  virtual Heard hear(const float* samples, size_t count, int64_t startMs, const HearOptions& options) = 0;
};

}  // namespace subtitle_engine
