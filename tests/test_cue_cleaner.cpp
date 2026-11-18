#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cue_cleaner.hpp"

using namespace subtitle_engine;

namespace {

std::vector<std::string> texts(const std::vector<Cue>& cues) {
  std::vector<std::string> out;
  for (const Cue& c : cues) out.push_back(c.text);
  return out;
}

Cue cue(int64_t start, int64_t end, std::string text) { return Cue{start, end, std::move(text)}; }

}  // namespace

TEST(CueCleaner, DropsSoundOnlyAndEmpty) {
  CueCleaner cleaner(CleanupRules{}, "");
  auto out = cleaner.clean({cue(0, 1000, "(music)"), cue(1000, 2000, "[door slams]"), cue(2000, 3000, "♪ la la"),
                            cue(3000, 4000, ""), cue(4000, 5000, "Hello.")});
  EXPECT_EQ(texts(out), std::vector<std::string>{"Hello."});
}

TEST(CueCleaner, TrimsLoopKeepsTwice) {
  CueCleaner cleaner(CleanupRules{}, "");
  auto out = cleaner.clean({cue(0, 1000, "no no no no no no"), cue(5000, 6000, "no, no, no, no")});
  EXPECT_EQ(texts(out), (std::vector<std::string>{"no, no", "no, no, no, no"}));
}

TEST(CueCleaner, DropsRepeatWithinWindow) {
  CueCleaner cleaner(CleanupRules{}, "");
  auto first = cleaner.clean({cue(1000, 2000, "Get out of here."), cue(12000, 13000, "Get out of here.")});
  EXPECT_EQ(texts(first), std::vector<std::string>{"Get out of here."});
  auto later = cleaner.clean({cue(17000, 18000, "Get out of here.")});
  EXPECT_EQ(texts(later), std::vector<std::string>{"Get out of here."});
}

TEST(CueCleaner, DropsImmediateRepeat) {
  CueCleaner cleaner(CleanupRules{}, "");
  auto out = cleaner.clean({cue(417700, 418700, "No!"), cue(418700, 419700, "No!")});
  EXPECT_EQ(texts(out), std::vector<std::string>{"No!"});
}

TEST(CueCleaner, KeepsRepeatAfterPause) {
  CueCleaner cleaner(CleanupRules{}, "");
  auto out = cleaner.clean({cue(417700, 418700, "No!"), cue(419500, 420500, "No!")});
  EXPECT_EQ(texts(out), (std::vector<std::string>{"No!", "No!"}));
  CueCleaner tags(CleanupRules{}, "");
  auto gasps = tags.clean({cue(0, 1000, "*Gasps*"), cue(3000, 4000, "*Gasps*")});
  EXPECT_EQ(texts(gasps), std::vector<std::string>{"*Gasps*"});
}

TEST(CueCleaner, KeepsShortRepeatWithinWindow) {
  CueCleaner cleaner(CleanupRules{}, "");
  auto out = cleaner.clean({cue(255500, 257500, "Guys?"), cue(262300, 264300, "Guys, this isn't funny."),
                            cue(265200, 267200, "Guys?")});
  EXPECT_EQ(texts(out), (std::vector<std::string>{"Guys?", "Guys, this isn't funny.", "Guys?"}));
}

TEST(CueCleaner, DropsCredits) {
  CueCleaner cleaner(CleanupRules{}, "");
  EXPECT_TRUE(cleaner.clean({cue(0, 1000, "Sous-titrage ST' 501")}).empty());
}

TEST(CueCleaner, DropsPromptNamesOnly) {
  CueCleaner cleaner(CleanupRules{}, "Barbra, Johnny, Ben");
  auto out = cleaner.clean({cue(0, 1000, "Barbra Johnny Ben"), cue(2000, 3000, "Ben, open the door")});
  EXPECT_EQ(texts(out), std::vector<std::string>{"Ben, open the door"});
}
