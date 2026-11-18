#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "file_source.hpp"
#include "subtitle_engine/audio_reader.hpp"

using namespace subtitle_engine;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

constexpr double kToneSamples = 20.0 * kSampleRate;  // the fixtures: 20 s of tone and silence

std::string fixture(const std::string& name) { return std::string(SE_FIXTURES) + "/" + name; }

std::unique_ptr<AudioReader> openReader(MediaSource& source, AudioReaderOptions options = {},
                                        std::string* error = nullptr) {
  std::string ignored;
  return AudioReader::open(source, std::move(options), error ? error : &ignored);
}

// Pulls until the reader stops; returns how it stopped.
PullResult pullAll(AudioReader& reader, size_t* total) {
  std::vector<float> samples;
  int64_t first = 0;
  for (;;) {
    samples.clear();
    PullResult r = reader.pull(&samples, &first);
    *total += samples.size();
    if (r != PullResult::kSamples) return r;
  }
}

long long msSince(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

}  // namespace

TEST(AudioReader, ReadsAc3Eac3Dts) {
  for (const char* name : {"audio_ac3.mkv", "audio_eac3.mkv", "audio_dts.mkv"}) {
    FileSource source(fixture(name));
    std::string error;
    auto reader = openReader(source, {}, &error);
    ASSERT_TRUE(reader) << name << ": " << error;
    size_t total = 0;
    EXPECT_EQ(pullAll(*reader, &total), PullResult::kEndOfMedia) << name;
    EXPECT_NEAR(static_cast<double>(total), kToneSamples, kToneSamples * 0.01) << name;
  }
}

TEST(AudioReader, ChannelChangeMidStream) {
  // Stereo for 10 s, then mono: the resampler must follow the frames, not the first one.
  FileSource source(fixture("audio_layout_change.mkv"));
  auto reader = openReader(source);
  ASSERT_TRUE(reader);
  size_t total = 0;
  EXPECT_EQ(pullAll(*reader, &total), PullResult::kEndOfMedia);
  EXPECT_NEAR(static_cast<double>(total), kToneSamples, kToneSamples * 0.01);
}

TEST(AudioReader, ListsTracksAndPicksDefault) {
  FileSource source(fixture("audio_two_tracks.mkv"));
  auto reader = openReader(source);
  ASSERT_TRUE(reader);
  EXPECT_EQ(reader->tracks().size(), 2u);
  EXPECT_EQ(reader->chosen().language, "eng");
  EXPECT_TRUE(reader->chosen().isDefault);
}

TEST(AudioReader, PicksTrackByLanguage) {
  FileSource source(fixture("audio_two_tracks.mkv"));
  AudioReaderOptions options;
  options.trackLanguage = "ita";
  auto reader = openReader(source, options);
  ASSERT_TRUE(reader);
  EXPECT_EQ(reader->chosen().language, "ita");
}

TEST(AudioReader, SeekKeepsFilmTime) {
  FileSource source(fixture("audio_ac3.mkv"));
  auto reader = openReader(source);
  ASSERT_TRUE(reader);
  ASSERT_TRUE(reader->seek(10000));
  std::vector<float> samples;
  int64_t first = -1;
  ASSERT_EQ(reader->pull(&samples, &first), PullResult::kSamples);
  EXPECT_LE(std::llabs(first - 10000), 25);
}

TEST(AudioReader, MoovAtEndNeedsEnd) {
  const std::string path = fixture("audio_moov_end.mp4");
  FileSource whole(path);
  FileSource partial(path, whole.size() * 9 / 10, ReadStatus::kEnd);
  std::string error;
  EXPECT_FALSE(openReader(partial, {}, &error));
  EXPECT_EQ(error, "needs_end_of_file");
  EXPECT_TRUE(openReader(whole, {}, &error)) << error;
}

TEST(AudioReader, NoCuesReadsUpToLimit) {
  const std::string path = fixture("audio_no_cues.mkv");
  FileSource whole(path);
  FileSource half(path, whole.size() / 2, ReadStatus::kEnd);
  auto reader = openReader(half);
  ASSERT_TRUE(reader);
  size_t total = 0;
  EXPECT_EQ(pullAll(*reader, &total), PullResult::kEndOfMedia);
  EXPECT_GT(total, kToneSamples * 0.4);
  EXPECT_LT(total, kToneSamples * 0.6);
}

TEST(AudioReader, WaitsThenContinues) {
  const std::string path = fixture("audio_ac3.mkv");
  size_t expected = 0;
  {
    FileSource whole(path);
    auto reader = openReader(whole);
    ASSERT_TRUE(reader);
    pullAll(*reader, &expected);
  }
  FileSource source(path, 0, ReadStatus::kWait);
  source.setLimit(FileSource(path).size() / 2);
  AudioReaderOptions options;
  options.waitPoll = 20ms;
  auto reader = openReader(source, options);
  ASSERT_TRUE(reader);
  const auto start = Clock::now();
  std::thread later([&] {
    std::this_thread::sleep_for(300ms);
    source.setLimit(INT64_MAX);
  });
  size_t total = 0;
  EXPECT_EQ(pullAll(*reader, &total), PullResult::kEndOfMedia);
  later.join();
  EXPECT_EQ(total, expected);
  EXPECT_GE(msSince(start), 300);
}

TEST(AudioReader, CancelWhileWaitingReturns) {
  const std::string path = fixture("audio_ac3.mkv");
  FileSource source(path, FileSource(path).size() / 2, ReadStatus::kWait);
  std::atomic<bool> cancel{false};
  std::atomic<long long> cancelledAt{0};
  const auto start = Clock::now();
  AudioReaderOptions options;
  options.waitPoll = 50ms;
  options.cancelled = [&] { return cancel.load(); };
  auto reader = openReader(source, options);
  ASSERT_TRUE(reader);
  std::thread later([&] {
    std::this_thread::sleep_for(200ms);
    cancelledAt = msSince(start);
    cancel = true;
  });
  size_t total = 0;
  const PullResult r = pullAll(*reader, &total);
  const long long returnedAt = msSince(start);
  later.join();
  EXPECT_EQ(r, PullResult::kCancelled);
  EXPECT_LE(returnedAt - cancelledAt.load(), 2 * 50);
}

TEST(AudioReader, ReadErrorIsIoError) {
  const std::string path = fixture("audio_ac3.mkv");
  FileSource source(path);
  source.failAt(source.size() * 3 / 10);
  auto reader = openReader(source);
  ASSERT_TRUE(reader);
  size_t total = 0;
  EXPECT_EQ(pullAll(*reader, &total), PullResult::kError);
  EXPECT_EQ(reader->lastErrorCode(), "io_error");
}

TEST(AudioReader, VideoOnlyIsNoAudio) {
  FileSource source(fixture("audio_none.mkv"));
  std::string error;
  EXPECT_FALSE(openReader(source, {}, &error));
  EXPECT_EQ(error, "no_audio");
}

TEST(AudioReader, UnknownCodecIsUnsupported) {
  FileSource source(fixture("audio_alac.mkv"));
  std::string error;
  EXPECT_FALSE(openReader(source, {}, &error));
  EXPECT_EQ(error, "unsupported_codec");
}
