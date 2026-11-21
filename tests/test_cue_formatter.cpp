#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "cue_formatter.hpp"

using namespace subtitle_engine;

namespace {

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  size_t at = 0;
  for (size_t nl; (nl = text.find('\n', at)) != std::string::npos; at = nl + 1) out.push_back(text.substr(at, nl - at));
  out.push_back(text.substr(at));
  return out;
}

}  // namespace

TEST(CueFormatter, SplitsLongAtSentenceEnd) {
  // 109 characters, more than two lines of 42, with a sentence end near the middle.
  const std::string first = "This sentence is the first one of the two and it goes on for a while.";
  const std::string second = "Then this one closes it, a bit shorter.";
  CueFormatter formatter(FormatRules{});
  auto out = formatter.format(std::nullopt, {{0, 12000, first + " " + second}});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(lines(out[0].text).size() <= 2, true);
  EXPECT_EQ(lines(out[1].text).size() <= 2, true);
  for (const auto& c : out)
    for (const auto& l : lines(c.text)) EXPECT_LE(l.size(), 42u) << l;
  // The time is shared by the characters: the first, longer part gets more of it.
  EXPECT_GT(out[0].endMs - out[0].startMs, out[1].endMs - out[1].startMs);
}

TEST(CueFormatter, WrapsTwoBalancedLines) {
  const std::string text = "We have to follow our passions, you have yours and I have mine";  // 62 characters
  CueFormatter formatter(FormatRules{});
  auto out = formatter.format(std::nullopt, {{0, 4000, text}});
  ASSERT_EQ(out.size(), 1u);
  auto ls = lines(out[0].text);
  ASSERT_EQ(ls.size(), 2u);
  EXPECT_LE(ls[0].size(), 42u);
  EXPECT_LE(ls[1].size(), 42u);
}

TEST(CueFormatter, ClampsOnScreenTime) {
  CueFormatter formatter(FormatRules{});
  auto out = formatter.format(std::nullopt, {{0, 10000, "A long one."}, {20000, 20400, "Short."}, {30000, 32000, "Far."}});
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0].endMs - out[0].startMs, 7000);
  EXPECT_EQ(out[1].endMs - out[1].startMs, 1000);
}

TEST(CueFormatter, NoOverlap) {
  CueFormatter formatter(FormatRules{});
  auto out = formatter.format(std::nullopt, {{0, 3000, "The first line."}, {2500, 5000, "The second line."}});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_GE(out[1].startMs - out[0].endMs, 83);
}

TEST(CueFormatter, KeepsGapAfterEmittedCue) {
  CueFormatter formatter(FormatRules{});
  auto out = formatter.format(Cue{3000, 5000, "Given out."}, {{4900, 7000, "The new one."}});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].startMs, 5083);
}

TEST(CueFormatter, ShortCueJoinsNext) {
  CueFormatter formatter(FormatRules{});
  auto out = formatter.format(std::nullopt, {{0, 300, "Hi."}, {700, 1500, "Hello there."}});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].text, "Hi. Hello there.");
  EXPECT_EQ(out[0].startMs, 0);
}
