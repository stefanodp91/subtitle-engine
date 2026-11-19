#include <gtest/gtest.h>

#include <cstdint>

#include "cue_timing.hpp"

using namespace subtitle_engine;

namespace {

constexpr int64_t kNoEnd = INT64_MAX;  // a piece's audio that ends far after the cue

void expectCue(const Cue& c, int64_t start, int64_t end) {
  EXPECT_EQ(c.startMs, start);
  EXPECT_EQ(c.endMs, end);
}

}  // namespace

TEST(CueTiming, StartsAtFirstWord) {  // BUG-289
  expectCue(alignCue({23000, 27000, "Hi."}, 24300, 26500, kNoEnd, TimingRules{}), 24300, 27000);
}

TEST(CueTiming, NeverBeforeWhisperStart) {
  expectCue(alignCue({23000, 27000, "Hi."}, 22500, 26500, kNoEnd, TimingRules{}), 23000, 27000);
}

TEST(CueTiming, StartStaysBeforeEnd) {
  expectCue(alignCue({23000, 27000, "Hi."}, 26900, 26950, kNoEnd, TimingRules{}), 26800, 27000);
}

TEST(CueTiming, MovesWholeWhenWordsAfterEnd) {
  expectCue(alignCue({100000, 101000, "Hi."}, 104000, 105200, kNoEnd, TimingRules{}), 104000, 105700);
  expectCue(alignCue({100000, 101000, "Hi."}, 104000, 104100, kNoEnd, TimingRules{}), 104000, 105000);
}

TEST(CueTiming, NoAlignmentKeepsWhisperTimes) {
  expectCue(alignCue({23000, 27000, "Hi."}, -1, -1, kNoEnd, TimingRules{}), 23000, 27000);
}

// BUG-310: the alignment heads can put words in the padding after a piece's audio;
// those times are not the words' times.
TEST(CueTiming, WordsPastTheAudioKeepWhisperTimes) {
  expectCue(alignCue({179000, 182000, "You have to learn to listen."}, 184280, 190780, 182385, TimingRules{}), 179000,
            182000);
}

TEST(CueTiming, LastWordNeverPastTheAudio) {
  // Moved whole, it still ends with the audio: the format gives the last cue of a step its second.
  expectCue(alignCue({100000, 101000, "Hi."}, 101500, 104000, 102000, TimingRules{}), 101500, 102000);
}

// BUG-310: whisper's own segment end can fall in the padding too (Elephants Dream:
// a cue heard in a piece ending at 182.385 s ended at 192.4 s).
TEST(CueTiming, WhisperEndNeverPastTheAudio) {
  expectCue(alignCue({175000, 192400, "Emo, look, you have to learn to listen."}, 175500, 181000, 182385, TimingRules{}),
            175500, 182385);
}

TEST(CueTiming, CueWhollyPastTheAudioEndsWithIt) {
  expectCue(alignCue({183000, 185000, "Hi."}, -1, -1, 182385, TimingRules{}), 182185, 182385);
}

TEST(CueTiming, ShortCueInsideTheAudioUntouched) {
  expectCue(alignCue({1000, 1100, "Oh."}, -1, -1, 182385, TimingRules{}), 1000, 1100);
}
