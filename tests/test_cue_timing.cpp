#include <gtest/gtest.h>

#include "cue_timing.hpp"

using namespace subtitle_engine;

namespace {

void expectCue(const Cue& c, int64_t start, int64_t end) {
  EXPECT_EQ(c.startMs, start);
  EXPECT_EQ(c.endMs, end);
}

}  // namespace

TEST(CueTiming, StartsAtFirstWord) {  // BUG-289
  expectCue(alignCue({23000, 27000, "Hi."}, 24300, 26500, TimingRules{}), 24300, 27000);
}

TEST(CueTiming, NeverBeforeWhisperStart) {
  expectCue(alignCue({23000, 27000, "Hi."}, 22500, 26500, TimingRules{}), 23000, 27000);
}

TEST(CueTiming, StartStaysBeforeEnd) {
  expectCue(alignCue({23000, 27000, "Hi."}, 26900, 26950, TimingRules{}), 26800, 27000);
}

TEST(CueTiming, MovesWholeWhenWordsAfterEnd) {
  expectCue(alignCue({100000, 101000, "Hi."}, 104000, 105200, TimingRules{}), 104000, 105700);
  expectCue(alignCue({100000, 101000, "Hi."}, 104000, 104100, TimingRules{}), 104000, 105000);
}

TEST(CueTiming, NoAlignmentKeepsWhisperTimes) {
  expectCue(alignCue({23000, 27000, "Hi."}, -1, -1, TimingRules{}), 23000, 27000);
}
