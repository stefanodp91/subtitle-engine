#include <gtest/gtest.h>

#include <vector>

#include "sentence_joiner.hpp"

using namespace subtitle_engine;

TEST(SentenceJoiner, JoinsAcrossCues) {
  auto out = joinSentences({{0, 2000, "I told you I was"}, {2000, 4000, "at the station. Where were you?"}}, JoinRules{});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].text, "I told you I was at the station.");
  EXPECT_EQ(out[0].startMs, 0);
  EXPECT_GE(out[0].endMs, 2800);  // the time of the cues spread over their characters
  EXPECT_LE(out[0].endMs, 3000);
  EXPECT_EQ(out[1].text, "Where were you?");
  EXPECT_EQ(out[1].endMs, 4000);
}

TEST(SentenceJoiner, KeepsUnfinishedTail) {
  auto out = joinSentences({{0, 2000, "Run. And then"}, {2000, 3000, "we waited"}}, JoinRules{});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[1].text, "And then we waited");
  EXPECT_EQ(out[1].endMs, 3000);
}

TEST(SentenceJoiner, ClosesAtLongPause) {  // BUG-300
  auto out = joinSentences({{48900, 53900, "Robot's memory synced and locked"},
                            {60900, 62900, "This is pretty freaky."}},
                           JoinRules{});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].text, "Robot's memory synced and locked");
  EXPECT_LE(out[0].endMs, 53900);
  EXPECT_EQ(out[1].startMs, 60900);
}

TEST(SentenceJoiner, JoinsAcrossShortPause) {
  auto out = joinSentences({{48900, 53900, "Robot's memory synced and locked"},
                            {54400, 56400, "This is pretty freaky."}},
                           JoinRules{});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].text, "Robot's memory synced and locked This is pretty freaky.");
}
