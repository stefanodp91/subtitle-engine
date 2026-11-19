#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fake_recognizer.hpp"
#include "file_source.hpp"
#include "subtitle_engine/subtitler.hpp"

using namespace subtitle_engine;
using namespace std::chrono_literals;

namespace {

std::string fixture(const std::string& name) { return std::string(SE_FIXTURES) + "/" + name; }

SubtitlerSpec spec(const std::string& target = "it") {
  SubtitlerSpec s;
  s.targetLanguage = target;
  s.config.waitPoll = 20ms;
  return s;
}

std::unique_ptr<Subtitler> openSubtitler(SpeechRecognizer& rec, MediaSource& source, SubtitlerSpec s,
                                         Progress progress = nullptr) {
  std::string error;
  auto sub = Subtitler::open(rec, source, std::move(s), std::move(progress), &error);
  EXPECT_TRUE(sub) << error;
  return sub;
}

bool hasText(const Step& step, const std::string& text) {
  for (const auto& c : step.cues)
    if (c.text.find(text) != std::string::npos) return true;
  return false;
}

class SubtitlerTest : public ::testing::Test {
 protected:
  FileSource source{fixture("audio_long.mkv")};
  FakeRecognizer rec;
  FakeTranslator translator;
};

}  // namespace

TEST_F(SubtitlerTest, FirstPieceIsShort) {
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  Step s = sub->step();
  EXPECT_EQ(s.status, StepStatus::kCues);
  EXPECT_LE(s.readyUntilMs - s.fromMs, 8000);
}

TEST_F(SubtitlerTest, FirstPieceHeardOnce) {
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  sub->step();
  EXPECT_EQ(rec.calls().size(), 1u);
}

TEST_F(SubtitlerTest, SecondListenUsesShorterPieces) {
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  sub->step();
  Step second = sub->step();
  int64_t secondListenEnd = 0;
  for (const auto& c : rec.calls())
    if (c.startMs >= 8000 && c.durationMs <= 14000) secondListenEnd = std::max(secondListenEnd, c.startMs + c.durationMs);
  EXPECT_GE(secondListenEnd, second.readyUntilMs);

  FakeRecognizer single;
  FileSource other(fixture("audio_long.mkv"));
  SubtitlerSpec s = spec();
  s.config.second.enabled = false;
  auto plain = openSubtitler(single, other, s);
  plain->setTranslator(&translator);
  plain->step();
  plain->step();
  plain->step();
  EXPECT_EQ(single.calls().size(), 3u);
}

TEST_F(SubtitlerTest, MergedCueAcrossBoundaryOnce) {
  rec.cues = {{23000, 25500, "Across the boundary."}};
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  int seen = 0;
  sub->step();               // 0 to 8 s
  Step second = sub->step();  // 8 to 24 s: the cue straddles the end
  EXPECT_LE(second.readyUntilMs, 23000);
  seen += hasText(second, "Across the boundary.");
  Step third = sub->step();
  seen += hasText(third, "Across the boundary.");
  EXPECT_EQ(seen, 1);
  EXPECT_TRUE(hasText(third, "IT:Across the boundary."));
}

TEST_F(SubtitlerTest, SameLanguageSkipsTranslation) {
  rec.cues = {{2000, 4000, "Hello there."}};
  auto sub = openSubtitler(rec, source, spec("en"));
  Step s = sub->step();
  ASSERT_EQ(s.status, StepStatus::kCues);
  ASSERT_FALSE(s.cues.empty());
  EXPECT_EQ(s.cues[0].text, "Hello there.");
}

TEST_F(SubtitlerTest, AsksForTranslator) {
  rec.cues = {{2000, 4000, "Hello there."}};
  auto sub = openSubtitler(rec, source, spec("it"));
  Step first = sub->step();
  EXPECT_EQ(first.status, StepStatus::kTranslatorNeeded);
  EXPECT_EQ(first.audioLanguage, "en");
  sub->setTranslator(&translator);
  Step again = sub->step();
  EXPECT_EQ(again.status, StepStatus::kCues);
  EXPECT_EQ(again.fromMs, first.fromMs);
  EXPECT_TRUE(hasText(again, "IT:Hello there."));
  EXPECT_EQ(rec.calls().size(), 1u);  // the piece is not heard again
}

TEST_F(SubtitlerTest, TrackLanguageWins) {
  rec.language = "fr";
  SubtitlerSpec s = spec();
  s.trackLanguage = "eng";
  auto sub = openSubtitler(rec, source, s);
  sub->setTranslator(&translator);
  sub->step();
  sub->step();
  for (const auto& c : rec.calls()) EXPECT_EQ(c.language, "en");
}

TEST_F(SubtitlerTest, MusicAdvancesReadyWithoutCues) {
  rec.script = {{0, 10000}, {40000, 120000}};
  rec.cues = {{2000, 4000, "Before the music."}, {45000, 47000, "After the music."}};
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  Step s;
  for (int i = 0; i < 10 && s.readyUntilMs <= 40000; ++i) {
    s = sub->step();
    ASSERT_EQ(s.status, StepStatus::kCues);
    for (const auto& c : s.cues) EXPECT_FALSE(c.startMs > 10000 && c.endMs < 40000) << c.text;
  }
  EXPECT_GT(s.readyUntilMs, 40000);
}

TEST_F(SubtitlerTest, SeekDuringStepDropsOldPiece) {
  rec.holdUntilCancelled = true;
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  Step held;
  std::thread worker([&] { held = sub->step(); });
  std::this_thread::sleep_for(100ms);
  sub->seek(60000);
  worker.join();
  EXPECT_EQ(held.status, StepStatus::kCancelled);
  rec.holdUntilCancelled = false;
  Step next = sub->step();
  EXPECT_EQ(next.fromMs, 60000);
  EXPECT_LE(next.readyUntilMs - next.fromMs, 8000);
}

TEST_F(SubtitlerTest, SeekInsideReadyKeepsGoing) {
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  sub->step();
  Step second = sub->step();
  sub->seek(second.readyUntilMs - 1000);
  Step third = sub->step();
  EXPECT_EQ(third.fromMs, second.readyUntilMs);
}

TEST_F(SubtitlerTest, StallHearsWhatArrived) {
  const std::string path = fixture("audio_long.mkv");
  FileSource slow(path, FileSource(path).size() / 10, ReadStatus::kWait);  // about 12 s of audio
  SubtitlerSpec s = spec();
  s.config.stall = 1000ms;
  s.config.waitPoll = 50ms;
  auto sub = openSubtitler(rec, slow, s);
  sub->setTranslator(&translator);
  Step step;
  for (int i = 0; i < 4 && step.readyUntilMs < 10000; ++i) step = sub->step();
  EXPECT_GE(step.readyUntilMs, 10000);
  EXPECT_LE(step.readyUntilMs, 14000);
  slow.setLimit(INT64_MAX);
  Step after = sub->step();
  EXPECT_LE(after.readyUntilMs - after.fromMs, 8000);  // DEC-844: short again after a stall
}

TEST_F(SubtitlerTest, RetryOnlyForListedLanguagesAfterFirstPiece) {
  rec.language = "ja";
  rec.cues = {{2000, 4000, "駅で待っていた。"}};  // speech in the first piece: the language is known from it
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  sub->step();
  sub->step();
  auto calls = rec.calls();
  ASSERT_GE(calls.size(), 2u);
  EXPECT_EQ(calls[0].temperatureInc, 0.0f);
  for (size_t i = 1; i < calls.size(); ++i) EXPECT_FLOAT_EQ(calls[i].temperatureInc, 0.2f);

  FakeRecognizer english;
  english.cues = {{2000, 4000, "I was waiting at the station."}};
  FileSource other(fixture("audio_long.mkv"));
  auto plain = openSubtitler(english, other, spec());
  plain->setTranslator(&translator);
  plain->step();
  plain->step();
  for (const auto& c : english.calls()) EXPECT_EQ(c.temperatureInc, 0.0f);
}

TEST_F(SubtitlerTest, EndOfMedia) {
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  Step s;
  for (int i = 0; i < 30 && s.status != StepStatus::kEndOfMedia; ++i) s = sub->step();
  EXPECT_EQ(s.status, StepStatus::kEndOfMedia);
  EXPECT_NEAR(static_cast<double>(s.readyUntilMs), 120000.0, 100.0);
}

TEST_F(SubtitlerTest, ProgressReportsStages) {
  rec.cues = {{2000, 4000, "Hello there."}};
  std::vector<std::pair<Stage, double>> seen;
  auto sub = openSubtitler(rec, source, spec(), [&](Stage st, double f) { seen.emplace_back(st, f); });
  sub->setTranslator(&translator);
  sub->step();
  ASSERT_FALSE(seen.empty());
  for (size_t i = 1; i < seen.size(); ++i) {
    EXPECT_GE(static_cast<int>(seen[i].first), static_cast<int>(seen[i - 1].first));
    EXPECT_GE(seen[i].second, seen[i - 1].second);
  }
  EXPECT_EQ(seen.front().first, Stage::kAudio);
  EXPECT_EQ(seen.back().first, Stage::kTranslating);
  EXPECT_DOUBLE_EQ(seen.back().second, 1.0);
}

TEST_F(SubtitlerTest, CuesNeverOverlapAcrossPieces) {
  // Different words in every line: the cleaning does not count digits as words.
  const std::vector<std::string> words{"apple", "bridge", "candle", "desert", "engine", "forest", "garden", "harbor",
                                       "island", "jungle", "kettle", "ladder", "meadow", "needle", "orchard", "pillow"};
  int64_t t = 1000;
  for (const auto& w : words) {
    rec.cues.push_back({t, t + 2000, "This line speaks about the " + w + " and goes on for a rather long while."});
    t += 2500;
  }
  auto sub = openSubtitler(rec, source, spec());
  sub->setTranslator(&translator);
  std::vector<Cue> all;
  for (int i = 0; i < 3; ++i) {
    Step s = sub->step();
    all.insert(all.end(), s.cues.begin(), s.cues.end());
  }
  ASSERT_GT(all.size(), 3u);
  for (size_t i = 0; i < all.size(); ++i) {
    const Cue& c = all[i];
    EXPECT_GE(c.endMs - c.startMs, 1000) << c.text;
    EXPECT_LE(c.endMs - c.startMs, 7000) << c.text;
    size_t lines = 1, line = 0;
    for (char ch : c.text) {
      if (ch == '\n') {
        ++lines;
        EXPECT_LE(line, 42u);
        line = 0;
      } else if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) {
        ++line;
      }
    }
    EXPECT_LE(line, 42u);
    EXPECT_LE(lines, 2u);
    if (i > 0) EXPECT_GE(c.startMs - all[i - 1].endMs, 83) << c.text;
  }
}
