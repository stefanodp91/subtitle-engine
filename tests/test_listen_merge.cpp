#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "listen_merge.hpp"

using namespace subtitle_engine;

namespace {

HeardCue heard(int64_t start, int64_t end, std::string text, double sure, int tokens = 3) {
  return HeardCue{Cue{start, end, std::move(text)}, sure, tokens};
}

bool keepAll(const std::vector<Cue>&) { return true; }

std::vector<std::string> texts(const std::vector<HeardCue>& cues) {
  std::vector<std::string> out;
  for (const auto& c : cues) out.push_back(c.cue.text);
  return out;
}

}  // namespace

TEST(ListenMerge, SurerListenWins) {
  auto out = mergeListens({heard(1000, 3000, "Hello there.", 0.6)}, {heard(1100, 2900, "Hello, Thom.", 0.9)},
                          DoubleListenRules{}, keepAll);
  EXPECT_EQ(texts(out), std::vector<std::string>{"Hello, Thom."});
}

TEST(ListenMerge, TieKeepsFirst) {
  auto out = mergeListens({heard(1000, 3000, "Hello there.", 0.8)}, {heard(1100, 2900, "Hello, Thom.", 0.8)},
                          DoubleListenRules{}, keepAll);
  EXPECT_EQ(texts(out), std::vector<std::string>{"Hello there."});
}

TEST(ListenMerge, OnlyFirstStays) {
  auto out = mergeListens({heard(1000, 3000, "Hello there.", 0.5), heard(5000, 6000, "Go.", 0.4)},
                          {heard(1100, 2900, "Hello there.", 0.5)}, DoubleListenRules{}, keepAll);
  EXPECT_EQ(texts(out), (std::vector<std::string>{"Hello there.", "Go."}));
}

TEST(ListenMerge, OnlySecondNeedsFillSure) {
  const std::vector<HeardCue> first{heard(1000, 3000, "Hello there.", 0.9)};
  auto unsure = mergeListens(first, {heard(5000, 6000, "Go.", 0.5)}, DoubleListenRules{}, keepAll);
  EXPECT_EQ(texts(unsure), std::vector<std::string>{"Hello there."});
  auto sure = mergeListens(first, {heard(5000, 6000, "Go.", 0.7)}, DoubleListenRules{}, keepAll);
  EXPECT_EQ(texts(sure), (std::vector<std::string>{"Hello there.", "Go."}));
}

TEST(ListenMerge, LoopLosesToOther) {
  std::string loop;
  for (int i = 0; i < 40; ++i) loop += "f****ing ";
  auto out = mergeListens({heard(1000, 9000, "Nothing to worry about.", 0.6)}, {heard(1000, 9000, loop, 0.99)},
                          DoubleListenRules{}, keepAll);
  EXPECT_EQ(texts(out), std::vector<std::string>{"Nothing to worry about."});
}

TEST(ListenMerge, RejectedWinnerLosesToOther) {
  auto keep = [](const std::vector<Cue>& cues) { return cues.empty() || cues[0].text != "Barbra Johnny Ben"; };
  auto out = mergeListens({heard(1000, 3000, "Ben, open the door.", 0.5)}, {heard(1000, 3000, "Barbra Johnny Ben", 0.95)},
                          DoubleListenRules{}, keep);
  EXPECT_EQ(texts(out), std::vector<std::string>{"Ben, open the door."});
}
