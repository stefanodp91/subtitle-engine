#pragma once
// The audio of a film, decoded with FFmpeg and resampled to what whisper hears:
// 16 kHz mono float, with the film time of every batch of samples.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "subtitle_engine/media_source.hpp"

namespace subtitle_engine {

constexpr int kSampleRate = 16000;  // whisper's input: 16 kHz mono float

struct AudioTrack {
  int index = -1;        // the stream's index in the container
  std::string codec;     // FFmpeg's name: ac3, eac3, dts, aac, opus…
  std::string language;  // ISO 639-2 as the container says it; "und" when it says nothing
  bool isDefault = false;
};

struct AudioReaderOptions {
  std::string trackLanguage;                                     // ISO 639-2 tag to prefer; else the default track, else the first
  std::chrono::milliseconds waitPoll{std::chrono::milliseconds(200)};  // how often a kWait is asked again
  std::function<bool()> cancelled;                               // checked while waiting
};

enum class PullResult { kSamples, kEndOfMedia, kCancelled, kError };

class AudioReader {
 public:
  /// errorCode: io_error, no_audio, unsupported_codec, needs_end_of_file, cancelled.
  static std::unique_ptr<AudioReader> open(MediaSource& source, AudioReaderOptions options, std::string* errorCode);
  ~AudioReader();
  AudioReader(const AudioReader&) = delete;
  AudioReader& operator=(const AudioReader&) = delete;

  const std::vector<AudioTrack>& tracks() const;
  const AudioTrack& chosen() const;
  /// The next samples start at positionMs, in film time. False when the container cannot seek there.
  bool seek(int64_t positionMs);
  /// Appends the next decoded samples (16 kHz mono) and the film time of the first of them.
  /// kError sets lastErrorCode(): io_error, or decode_error for a stream FFmpeg cannot decode.
  PullResult pull(std::vector<float>* samples, int64_t* firstSampleMs);
  const std::string& lastErrorCode() const;

 private:
  struct Impl;
  explicit AudioReader(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace subtitle_engine
