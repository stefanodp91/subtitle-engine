#pragma once
// The film's audio read ahead on a thread of its own: an AudioReader pulled
// into a buffer of samples with their film time. A seek clears the buffer and
// moves the reader; a wait for bytes not there yet goes on in the reader (FFmpeg
// does not take a "try again" in the middle of a packet), so a stall is told by
// how long no samples have come, not by the bytes.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "subtitle_engine/audio_reader.hpp"

namespace subtitle_engine {

class AudioFeed {
 public:
  /// errorCode as AudioReader::open.
  /// readAhead: how far past the oldest sample still needed the feed reads.
  static std::unique_ptr<AudioFeed> open(MediaSource& source, const std::string& trackLanguage,
                                         std::chrono::milliseconds waitPoll, std::chrono::milliseconds readAhead,
                                         std::string* errorCode);
  ~AudioFeed();
  AudioFeed(const AudioFeed&) = delete;
  AudioFeed& operator=(const AudioFeed&) = delete;

  const AudioTrack& track() const { return reader_->chosen(); }

  struct State {
    int64_t startMs = 0;      // the film time of the first sample buffered
    int64_t endMs = 0;        // the film time after the last one
    bool ended = false;       // no more samples after endMs
    std::string error;        // io_error… when the reading failed
    std::string endDetail;    // why it ended, in numbers (AudioReader::endDetail)
    std::chrono::milliseconds idle{0};  // since the last samples came
  };
  State state() const;
  /// The samples from fromMs to toMs (within the buffer).
  std::vector<float> samples(int64_t fromMs, int64_t toMs) const;
  void dropBefore(int64_t ms);  // the samples before ms are no longer needed
  void seek(int64_t positionMs);  // any thread
  /// Waits until new samples, the end or an error, at most for timeout.
  void waitForChange(std::chrono::milliseconds timeout) const;

 private:
  AudioFeed(std::unique_ptr<AudioReader> reader, std::chrono::milliseconds readAhead);
  void run();
  bool interrupted() const { return closing_ || seekTo_ >= 0; }

  std::unique_ptr<AudioReader> reader_;
  size_t maxBuffered_;
  mutable std::mutex mutex_;
  mutable std::condition_variable changed_;
  std::deque<float> buffer_;
  int64_t startMs_ = 0;
  bool startKnown_ = false;
  bool ended_ = false;
  std::string error_;
  std::string endDetail_;
  std::chrono::steady_clock::time_point lastArrival_;
  std::atomic<int64_t> seekTo_{-1};
  std::atomic<bool> closing_{false};
  std::thread thread_;
};

}  // namespace subtitle_engine
