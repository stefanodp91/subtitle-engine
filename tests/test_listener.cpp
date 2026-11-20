#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "file_source.hpp"
#include "subtitle_engine/audio_reader.hpp"
#include "subtitle_engine/listener.hpp"

using namespace subtitle_engine;
using Clock = std::chrono::steady_clock;

namespace {

std::string fixture(const std::string& name) { return std::string(SE_FIXTURES) + "/" + name; }
bool exists(const std::string& path) { return std::ifstream(path).good(); }

// The whisper base model for speed (the small one only on the films), with its own alignment heads.
ListenerSpec baseSpec() {
  ListenerSpec spec;
  spec.modelPath = fixture("whisper/ggml-base-q5_1.bin");
  spec.vadPath = fixture("whisper/ggml-silero-v6.2.0.bin");
  spec.heads = AlignmentHeads::kBase;
  return spec;
}

// durationMs of a fixture's audio from fromMs, as whisper hears it.
std::vector<float> samples(const std::string& name, int64_t fromMs, int64_t durationMs) {
  FileSource source(fixture(name));
  std::string error;
  auto reader = AudioReader::open(source, {}, &error);
  EXPECT_TRUE(reader) << error;
  std::vector<float> out;
  if (!reader) return out;
  if (fromMs > 0) EXPECT_TRUE(reader->seek(fromMs));
  const size_t want = static_cast<size_t>(durationMs * kSampleRate / 1000);
  int64_t first = 0;
  while (out.size() < want && reader->pull(&out, &first) == PullResult::kSamples) {
  }
  if (out.size() > want) out.resize(want);
  return out;
}

class ListenerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!exists(fixture("whisper/ggml-base-q5_1.bin")) || !exists(fixture("speech/english.mkv")))
      GTEST_SKIP() << "run scripts/fetch_test_models.sh and scripts/make_speech_fixtures.sh";
    std::string error;
    listener_ = Listener::open(baseSpec(), &error);
    ASSERT_TRUE(listener_) << error;
  }
  std::unique_ptr<Listener> listener_;
};

std::string allText(const Heard& heard) {
  std::string out;
  for (const auto& c : heard.cues) out += c.cue.text + " ";
  return out;
}

}  // namespace

TEST_F(ListenerTest, HearsEnglishSentence) {
  auto audio = samples("speech/english.mkv", 0, 8000);
  Heard heard = listener_->hear(audio.data(), audio.size(), 0, {});
  EXPECT_NE(allText(heard).find("station"), std::string::npos) << allText(heard);
  EXPECT_EQ(heard.detectedLanguage, "en");
  EXPECT_TRUE(heard.spoken);
  EXPECT_FALSE(heard.cancelled);
}

TEST_F(ListenerTest, ReportsConfidence) {  // DEC-886
  auto audio = samples("speech/english.mkv", 0, 8000);
  Heard heard = listener_->hear(audio.data(), audio.size(), 0, {});
  ASSERT_FALSE(heard.cues.empty());
  for (const auto& c : heard.cues) {
    EXPECT_GT(c.sure, 0.0);
    EXPECT_LE(c.sure, 1.0);
    EXPECT_GT(c.tokens, 0);
  }
}

TEST_F(ListenerTest, DetectsItalian) {
  auto audio = samples("speech/italian.mkv", 0, 10000);
  EXPECT_EQ(listener_->hear(audio.data(), audio.size(), 0, {}).detectedLanguage, "it");
}

TEST_F(ListenerTest, KeepsFilmTime) {
  auto audio = samples("speech/english.mkv", 60000, 8000);
  HearOptions options;
  options.language = "en";
  Heard heard = listener_->hear(audio.data(), audio.size(), 60000, options);
  ASSERT_FALSE(heard.cues.empty());
  EXPECT_GE(heard.cues.front().cue.startMs, 60000);
}

TEST_F(ListenerTest, CancelStopsQuickly) {
  auto audio = samples("speech/english.mkv", 0, 20000);
  std::atomic<bool> cancel{false};
  HearOptions options;
  options.cancelled = [&] { return cancel.load(); };
  std::thread later([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    cancel = true;
  });
  const auto start = Clock::now();
  Heard heard = listener_->hear(audio.data(), audio.size(), 0, options);
  later.join();
  EXPECT_TRUE(heard.cancelled);
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count(), 1000);
}

TEST(Listener, MissingModelFails) {
  ListenerSpec spec = baseSpec();
  spec.modelPath = fixture("whisper/no-such-model.bin");
  std::string error;
  EXPECT_FALSE(Listener::open(spec, &error));
  EXPECT_EQ(error, "model_load_failed");
}
