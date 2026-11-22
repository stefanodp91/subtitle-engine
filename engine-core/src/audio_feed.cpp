#include "audio_feed.hpp"

#include <algorithm>

namespace subtitle_engine {

namespace {
int64_t samplesToMs(size_t n) { return static_cast<int64_t>(n) * 1000 / kSampleRate; }
size_t msToSamples(int64_t ms) { return static_cast<size_t>(std::max<int64_t>(0, ms) * kSampleRate / 1000); }
}  // namespace

std::unique_ptr<AudioFeed> AudioFeed::open(MediaSource& source, const std::string& trackLanguage,
                                           std::chrono::milliseconds waitPoll, std::chrono::milliseconds readAhead,
                                           std::string* errorCode) {
  // The reader's waits end only for a seek or the end of the feed, set once the feed exists.
  auto interrupt = std::make_shared<AudioFeed*>(nullptr);
  AudioReaderOptions options;
  options.trackLanguage = trackLanguage;
  options.waitPoll = waitPoll;
  options.cancelled = [interrupt] { return *interrupt && (*interrupt)->interrupted(); };
  auto reader = AudioReader::open(source, options, errorCode);
  if (!reader) return nullptr;
  std::unique_ptr<AudioFeed> feed(new AudioFeed(std::move(reader), readAhead));
  *interrupt = feed.get();
  feed->thread_ = std::thread(&AudioFeed::run, feed.get());
  return feed;
}

AudioFeed::AudioFeed(std::unique_ptr<AudioReader> reader, std::chrono::milliseconds readAhead)
    : reader_(std::move(reader)), maxBuffered_(msToSamples(readAhead.count())),
      lastArrival_(std::chrono::steady_clock::now()) {}

AudioFeed::~AudioFeed() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    closing_ = true;
  }
  changed_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void AudioFeed::run() {
  std::vector<float> got;
  while (!closing_) {
    const int64_t target = seekTo_.exchange(-1);
    if (target >= 0) {
      const bool moved = reader_->seek(target);
      std::lock_guard<std::mutex> lock(mutex_);
      buffer_.clear();
      startMs_ = target;
      startKnown_ = true;
      ended_ = false;
      error_ = moved ? "" : "io_error";
      lastArrival_ = std::chrono::steady_clock::now();
      changed_.notify_all();
    }
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (ended_ || !error_.empty()) {
        changed_.wait(lock, [&] { return closing_ || seekTo_ >= 0; });
        continue;
      }
      if (buffer_.size() >= maxBuffered_) {  // far enough ahead: wait until the queue takes some
        changed_.wait(lock, [&] { return closing_ || seekTo_ >= 0 || buffer_.size() < maxBuffered_; });
        lastArrival_ = std::chrono::steady_clock::now();  // a full buffer is not a stalled download
        continue;
      }
    }
    got.clear();
    int64_t first = -1;
    const PullResult r = reader_->pull(&got, &first);
    std::lock_guard<std::mutex> lock(mutex_);
    if (interrupted()) continue;  // a seek or the end: what came belongs to the old place
    if (r == PullResult::kSamples) {
      if (!startKnown_ || (buffer_.empty() && first >= 0)) {
        startMs_ = std::max<int64_t>(0, first);
        startKnown_ = true;
      }
      buffer_.insert(buffer_.end(), got.begin(), got.end());
      lastArrival_ = std::chrono::steady_clock::now();
    } else if (r == PullResult::kEndOfMedia) {
      ended_ = true;
      endDetail_ = reader_->endDetail();
    } else if (r == PullResult::kError) {
      error_ = reader_->lastErrorCode();
    }
    changed_.notify_all();
  }
}

AudioFeed::State AudioFeed::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  State s;
  s.startMs = startMs_;
  s.endMs = startMs_ + samplesToMs(buffer_.size());
  s.ended = ended_ && seekTo_ < 0;
  s.endDetail = s.ended ? endDetail_ : "";
  s.error = seekTo_ < 0 ? error_ : "";
  s.idle = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastArrival_);
  return s;
}

std::vector<float> AudioFeed::samples(int64_t fromMs, int64_t toMs) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t a = std::min(buffer_.size(), msToSamples(fromMs - startMs_));
  const size_t b = std::min(buffer_.size(), msToSamples(toMs - startMs_));
  return std::vector<float>(buffer_.begin() + static_cast<long>(a), buffer_.begin() + static_cast<long>(std::max(a, b)));
}

void AudioFeed::dropBefore(int64_t ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t n = std::min(buffer_.size(), msToSamples(ms - startMs_));
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(n));
  startMs_ += samplesToMs(n);
  changed_.notify_all();
}

void AudioFeed::seek(int64_t positionMs) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    startMs_ = positionMs;
    startKnown_ = true;
    ended_ = false;
    error_.clear();
    lastArrival_ = std::chrono::steady_clock::now();
    seekTo_ = positionMs;
  }
  changed_.notify_all();
}

void AudioFeed::waitForChange(std::chrono::milliseconds timeout) const {
  std::unique_lock<std::mutex> lock(mutex_);
  changed_.wait_for(lock, timeout);
}

}  // namespace subtitle_engine
