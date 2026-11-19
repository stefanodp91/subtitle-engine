#pragma once
// The insides of the queue, shared by subtitler.cpp (the step, jumps,
// translation) and subtitler_listen.cpp (pieces, listening, the double listen).

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "audio_feed.hpp"
#include "cue_cleaner.hpp"
#include "cue_formatter.hpp"
#include "ready_ranges.hpp"
#include "subtitle_engine/subtitler.hpp"

namespace subtitle_engine {

using Stale = std::function<bool()>;  // the step was cancelled or the film jumped

struct Cursor {  // one listen's place in the film
  int64_t atMs = 0;
  int sinceRamp = 0;          // pieces since the start, a jump or a stall
  int64_t lastProbeMs = -1;   // the audio the first piece last looked for a pause in
};

struct Piece {
  enum class Kind { kAudio, kEnd, kCancelled, kError } kind = Kind::kEnd;
  int64_t atMs = 0, endMs = 0;
  std::vector<float> samples;
  bool ramp = false;  // the first piece after the start, a jump or a stall
  std::string error;
};

struct Listened {  // what one step heard, merged and final
  Piece::Kind kind = Piece::Kind::kAudio;  // kEnd: the media ended
  std::vector<Cue> cues;
  int64_t readyUntilMs = 0;
  std::string error;
};

struct Subtitler::Impl {
  Impl(SpeechRecognizer& r, SubtitlerSpec s, Progress p)
      : recognizer(r), spec(std::move(s)), progress(std::move(p)), cleaner(spec.config.cleanup, spec.prompt),
        formatter(spec.config.format) {}

  // subtitler_listen.cpp
  Listened listen(const Stale& stale);
  Piece waitPiece(Cursor& cursor, const PieceRules& rules, const Stale& stale);
  bool hearInto(const Piece& piece, std::vector<HeardCue>* into, const Stale& stale);
  bool keeps(const std::vector<Cue>& cues) const;

  // subtitler.cpp
  void report(Stage stage, double fraction);
  void restartAt(int64_t positionMs);
  Step finish(Step step, std::vector<Sentence> sentences, int64_t readyUntilMs, bool ended);

  SpeechRecognizer& recognizer;
  SubtitlerSpec spec;
  Progress progress;
  std::unique_ptr<AudioFeed> feed;
  TextTranslator* translator = nullptr;

  std::atomic<uint64_t> generation{0};
  std::atomic<bool> cancelled{false};
  std::atomic<int64_t> pendingSeek{-1};
  std::mutex readyMutex;  // ready is asked by seek() from any thread
  ReadyRanges ready;

  CueCleaner cleaner;
  CueFormatter formatter;
  std::optional<Cue> previous;  // the last cue given out
  Cursor first, second;
  std::vector<HeardCue> carryFirst, carrySecond;  // heard, in stretches not final yet
  std::string language;                           // whisper's code, once known
  int heardPieces = 0;
  int64_t nextFromMs = 0;
  bool ended = false;

  struct Waiting {  // heard, cleaned and joined, waiting for a translator
    Step step;
    std::vector<Sentence> sentences;
    int64_t readyUntilMs = 0;
    bool ended = false;
  };
  std::optional<Waiting> waiting;

  Stage lastStage = Stage::kAudio;
  double lastFraction = 0;
};

}  // namespace subtitle_engine
