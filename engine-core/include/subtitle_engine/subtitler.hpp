#pragma once
// The heard-to-translated queue: the film's audio heard piece by piece, cleaned,
// joined into sentences, translated when its language is not the one asked for,
// shaped for the screen; jumps, stalls of the download and the double listen.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "subtitle_engine/cue.hpp"
#include "subtitle_engine/cue_rules.hpp"
#include "subtitle_engine/media_source.hpp"
#include "subtitle_engine/speech_recognizer.hpp"
#include "subtitle_engine/text_translator.hpp"

namespace subtitle_engine {

struct SubtitlerConfig {
  PieceRules pieces;
  CleanupRules cleanup;
  JoinRules join;
  FormatRules format;
  DoubleListenRules second;
  RehearRules rehear;                              // BUG-307
  std::chrono::milliseconds stall{20000};          // DEC-843
  std::chrono::milliseconds waitPoll{200};
  std::chrono::milliseconds readAhead{60000};      // how much audio is decoded ahead of the queue
  std::vector<std::string> retryLanguages{"ja"};   // DEC-847
  float temperatureInc = 0.2f;
};

struct SubtitlerSpec {
  std::string targetLanguage;  // BCP 47
  std::string trackLanguage;   // ISO 639-2 of the track to hear; empty: the file's default track and its tag
  std::string prompt;          // the characters' names
  std::string storePath;       // where the subtitles generated so far are kept; empty: nowhere
  std::string mediaId;         // the fingerprint of the film's version, from the caller (DEC-858)
  SubtitlerConfig config;
};

enum class StepStatus { kCues, kEndOfMedia, kCancelled, kTranslatorNeeded, kError };

struct Step {
  StepStatus status = StepStatus::kCues;
  int64_t fromMs = 0, readyUntilMs = 0;
  std::string audioLanguage, errorCode;
  std::vector<Cue> cues;
};

enum class Stage { kAudio, kListening, kTranslating };
using Progress = std::function<void(Stage stage, double fraction)>;

class Subtitler {
 public:
  /// errorCode as AudioReader::open. With a store the stretches saved for the same version and
  /// target language are skipped; a file of another version, or broken, is started over.
  static std::unique_ptr<Subtitler> open(SpeechRecognizer& recognizer, MediaSource& source, SubtitlerSpec spec,
                                         Progress progress, std::string* errorCode);
  ~Subtitler();
  Subtitler(const Subtitler&) = delete;
  Subtitler& operator=(const Subtitler&) = delete;

  void setTranslator(TextTranslator* translator);  // not owned; null when the audio is in the target language
  void seek(int64_t positionMs);  // any thread: outside the ready stretch, the running step returns kCancelled
  void cancel();                  // any thread
  Step step();                    // blocking: hears, cleans, translates and shapes one piece, then saves

  struct Impl;

 private:
  explicit Subtitler(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace subtitle_engine
