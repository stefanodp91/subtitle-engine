#include <gtest/gtest.h>

#include <vector>

#include "piece_cutter.hpp"

using namespace subtitle_engine;

namespace {

// Speech everywhere but in pauses 400 ms wide centred on the given times.
std::vector<SpeechSpan> speechWithPausesAt(int64_t untilMs, const std::vector<int64_t>& pauseCentres) {
  std::vector<SpeechSpan> out;
  int64_t from = 0;
  for (int64_t c : pauseCentres) {
    out.push_back({from, c - 200});
    from = c + 200;
  }
  out.push_back({from, untilMs});
  return out;
}

}  // namespace

TEST(PieceCutter, FirstPieceClosesAtFirstPause) {
  EXPECT_EQ(cutPoint(PieceRules{}, 2500, {{0, 1700}, {2100, 5000}}, 0, false), 1900);
}

TEST(PieceCutter, FirstPieceWaitsWithoutPause) {
  EXPECT_EQ(cutPoint(PieceRules{}, 2500, {{0, 2500}}, 0, false), std::nullopt);
  EXPECT_EQ(cutPoint(PieceRules{}, 8000, {{0, 8000}}, 0, false), 8000);
}

TEST(PieceCutter, GrowsToFullPiece) {
  EXPECT_EQ(cutPoint(PieceRules{}, 25000, {{0, 25000}}, 1, false), 16000);
  EXPECT_EQ(cutPoint(PieceRules{}, 25000, {{0, 25000}}, 2, false), 20000);
}

TEST(PieceCutter, CutsAtLastPauseAfterCutMin) {
  EXPECT_EQ(cutPoint(PieceRules{}, 20000, speechWithPausesAt(20000, {5000, 9000, 15000}), 3, false), 15000);
}

TEST(PieceCutter, NoPauseCutsAtPiece) {
  EXPECT_EQ(cutPoint(PieceRules{}, 20000, {{0, 20000}}, 3, false), 20000);
}

TEST(PieceCutter, EndOfAudioTakesRest) {
  EXPECT_EQ(cutPoint(PieceRules{}, 600, {{0, 600}}, 3, true), 600);
  EXPECT_EQ(cutPoint(PieceRules{}, 400, {{0, 400}}, 3, true), 0);  // too short: dropped
}
