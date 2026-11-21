#include <gtest/gtest.h>

#include <functional>
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

namespace {

// What the voice detector hears of a piece with speech in [0, 2900] and [3500, 9000] ms,
// when given the audio up to untilMs.
std::vector<SpeechSpan> heardUntil(int64_t untilMs) {
  std::vector<SpeechSpan> out;
  for (const SpeechSpan& s : std::vector<SpeechSpan>{{0, 2900}, {3500, 9000}})
    if (s.startMs < untilMs) out.push_back({s.startMs, std::min(s.endMs, untilMs)});
  return out;
}

// The first cut when the audio arrives up to each of the given amounts, one after the other.
std::optional<int64_t> firstCutWhenArriving(const std::vector<int64_t>& arrivals) {
  int64_t lastProbe = -1;
  for (int64_t buffered : arrivals)
    if (auto cut = firstCutPoint(PieceRules{}, buffered, &lastProbe, heardUntil)) return cut;
  return std::nullopt;
}

}  // namespace

// BUG-311: the first cut does not depend on when the audio arrived.
TEST(PieceCutter, FirstCutSameWhateverTheArrival) {
  EXPECT_EQ(firstCutWhenArriving({3200, 3700}), 3100);
  EXPECT_EQ(firstCutWhenArriving({2000, 2500, 3000, 3700}), 3100);
  EXPECT_EQ(firstCutWhenArriving({9000}), 3100);
}

TEST(PieceCutter, FirstCutLooksOnlyAtFixedPoints) {
  // 3200 ms arrived: the points 1800, 2300 and 2800 hear no pause yet; 3300 is not there.
  int64_t lastProbe = -1;
  EXPECT_EQ(firstCutPoint(PieceRules{}, 3200, &lastProbe, heardUntil), std::nullopt);
  EXPECT_EQ(lastProbe, 2800);
}

TEST(PieceCutter, FirstCutNotBeforeFirstCut) {
  int64_t lastProbe = -1;
  EXPECT_EQ(firstCutPoint(PieceRules{}, 1700, &lastProbe, heardUntil), std::nullopt);
  EXPECT_EQ(lastProbe, -1);
}
