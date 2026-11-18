#pragma once
// whisper.cpp in the engine: the voice detector (Silero) and the model that
// writes what is said, with the times of its words from the alignment heads.

#include <cstddef>
#include <memory>
#include <string>

#include "subtitle_engine/cue_rules.hpp"
#include "subtitle_engine/speech_recognizer.hpp"

namespace subtitle_engine {

enum class AlignmentHeads { kNone, kBase, kSmall, kLargeV3Turbo };  // whisper's alignment heads (DTW) for the model

struct ListenerSpec {
  std::string modelPath, vadPath;
  int threads = 2;
  int vadThreads = 1;
  AlignmentHeads heads = AlignmentHeads::kSmall;
  TimingRules timing;
};

class Listener : public SpeechRecognizer {
 public:
  /// errorCode: invalid_argument, model_load_failed.
  static std::unique_ptr<Listener> open(const ListenerSpec& spec, std::string* errorCode);
  ~Listener() override;
  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;

  std::vector<SpeechSpan> speech(const float* samples, size_t count) override;
  Heard hear(const float* samples, size_t count, int64_t startMs, const HearOptions& options) override;

 private:
  struct Impl;
  explicit Listener(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace subtitle_engine
