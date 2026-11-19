#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "dense_cue.hpp"

using namespace subtitle_engine;
using namespace std::chrono_literals;

namespace {

HeardCue cue(int64_t start, int64_t end, const std::string& text) { return {{start, end, text}, 0.9, 5}; }

const std::string kPacked = "They're gonna kill us! We're just gonna catch one!";  // 50 characters

std::optional<int64_t> from(const std::vector<HeardCue>& cues, int64_t pieceStart = 0) {
  return rehearFrom(cues, pieceStart, RehearRules{}, 500ms, DoubleListenRules{}.loopRatio);
}

}  // namespace

TEST(DenseCue, RehearsFromPackedLastCue) {
  EXPECT_EQ(from({cue(1000, 3000, "A normal line of speech."), cue(7800, 8000, kPacked)}), 7800);
}

TEST(DenseCue, ShortInterjectionStays) {
  EXPECT_EQ(from({cue(1000, 3000, "A normal line of speech."), cue(7950, 8000, "Grace?")}), std::nullopt);
}

TEST(DenseCue, SpeechAtItsPaceStays) {
  // 50 characters in 2 s: 25 a second, fast speech but speech.
  EXPECT_EQ(from({cue(6000, 8000, kPacked)}), std::nullopt);
}

TEST(DenseCue, LoopLeftToCleaning) {
  std::string loop;
  for (int i = 0; i < 12; ++i) loop += "Listen! ";
  EXPECT_EQ(from({cue(7800, 8000, loop)}), std::nullopt);
}

TEST(DenseCue, DenseAtPieceStartStays) {
  // Hearing it again from there would not move on: it is given out as it is.
  EXPECT_EQ(from({cue(20300, 20500, kPacked)}, 20000), std::nullopt);
}

TEST(DenseCue, EarliestDenseCueWins) {
  EXPECT_EQ(from({cue(9000, 9200, kPacked), cue(4000, 4200, kPacked), cue(1000, 3000, "A normal line.")}), 4000);
}

TEST(DenseCue, CountsCodePoints) {
  // 15 code points (45 bytes) in 0.2 s: dense; 12 code points (36 bytes): too short to judge.
  EXPECT_EQ(from({cue(7800, 8000, "こんにちは、元気ですか。今日は")}), 7800);
  EXPECT_EQ(from({cue(7800, 8000, "こんにちは、元気ですか。")}), std::nullopt);
}
