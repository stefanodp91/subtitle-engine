#pragma once
// A SpeechRecognizer for the queue's tests: the speech and the cues come from a
// script by film time, not from the audio; every call is recorded, and a call
// can be held until it is cancelled.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "subtitle_engine/speech_recognizer.hpp"
#include "subtitle_engine/text_translator.hpp"

struct ScriptCue {
  int64_t startMs, endMs;
  std::string text;
  double sure = 0.9;
  int tokens = 5;
  // Heard only by a piece that starts in [pieceFromMs, pieceToMs]: whisper hears a
  // stretch differently depending on where its piece starts (BUG-307).
  int64_t pieceFromMs = INT64_MIN, pieceToMs = INT64_MAX;
};

class FakeRecognizer : public subtitle_engine::SpeechRecognizer {
 public:
  struct Call {
    int64_t startMs, durationMs;
    std::string language;
    float temperatureInc;
  };

  std::string language = "en";                         // what it detects
  std::string musicLanguage;  // what it detects in a piece with no speech; empty: language
  std::vector<subtitle_engine::SpeechSpan> script{{0, 1000000}};  // speech, in film time
  std::vector<ScriptCue> cues;                          // in film time
  std::atomic<bool> holdUntilCancelled{false};

  std::vector<subtitle_engine::SpeechSpan> speech(const float*, size_t count, int64_t startMs) override {
    const int64_t end = startMs + static_cast<int64_t>(count) * 1000 / 16000;
    std::vector<subtitle_engine::SpeechSpan> out;
    for (const auto& s : script)
      if (s.endMs > startMs && s.startMs < end)
        out.push_back({std::max(s.startMs, startMs) - startMs, std::min(s.endMs, end) - startMs});
    return out;
  }

  subtitle_engine::Heard hear(const float*, size_t count, int64_t startMs,
                              const subtitle_engine::HearOptions& options) override {
    const int64_t duration = static_cast<int64_t>(count) * 1000 / 16000;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      calls_.push_back({startMs, duration, options.language, options.temperatureInc});
    }
    subtitle_engine::Heard heard;
    while (holdUntilCancelled && !(options.cancelled && options.cancelled()))
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (options.cancelled && options.cancelled()) {
      heard.cancelled = true;
      return heard;
    }
    const bool voiced = !speech(nullptr, count, startMs).empty();
    heard.detectedLanguage = !options.language.empty()                  ? options.language
                             : !voiced && !musicLanguage.empty() ? musicLanguage
                                                                  : language;
    for (const auto& c : cues)  // whisper writes what starts in the piece
      if (c.startMs >= startMs && c.startMs < startMs + duration && startMs >= c.pieceFromMs && startMs <= c.pieceToMs)
        heard.cues.push_back({{c.startMs, c.endMs, c.text}, c.sure, c.tokens});
    heard.spoken = !heard.cues.empty();
    return heard;
  }

  std::vector<Call> calls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<Call> calls_;
};

class FakeTranslator : public subtitle_engine::TextTranslator {
 public:
  std::vector<std::string> translate(const std::vector<std::string>& lines) override {
    std::vector<std::string> out;
    for (const auto& l : lines) out.push_back("IT:" + l);
    return out;
  }
};
