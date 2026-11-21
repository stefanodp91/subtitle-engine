#pragma once
// A subtitle on screen, and a sentence of heard speech with its time.

#include <cstdint>
#include <string>

namespace subtitle_engine {

struct Cue {
  int64_t startMs = 0;
  int64_t endMs = 0;
  std::string text;  // '\n' between rows; ASS override tags {\…} kept
};

/// A whole sentence, joined from heard cues, with the time it is said.
struct Sentence {
  int64_t startMs = 0;
  int64_t endMs = 0;
  std::string text;
};

}  // namespace subtitle_engine
