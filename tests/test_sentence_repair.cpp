#include <gtest/gtest.h>

#include "sentence_repair.hpp"

using subtitle_engine::repairSentence;

TEST(SentenceRepair, KeepsOneCopyOfADoubledSentence) {
  EXPECT_EQ(repairSentence("What?", "Cosa? Cosa?"), "Cosa?");
  EXPECT_EQ(repairSentence("Thanks.", "Grazie. Grazie."), "Grazie.");
}

TEST(SentenceRepair, KeepsOneCopyWhenOnlyPunctuationOrCaseDiffers) {
  EXPECT_EQ(repairSentence("Yes?", "Sì? Sì."), "Sì?");
  EXPECT_EQ(repairSentence("Yes!", "Sì! sì!"), "Sì!");
}

TEST(SentenceRepair, KeepsOneCopyOfAThreefoldSentence) { EXPECT_EQ(repairSentence("Go!", "Vai! Vai! Vai!"), "Vai!"); }

TEST(SentenceRepair, KeepsOneCopyOfADoubledGroup) {
  EXPECT_EQ(repairSentence("Wait, stop!", "Aspetta. Fermati! Aspetta. Fermati!"), "Aspetta. Fermati!");
}

TEST(SentenceRepair, KeepsARepetitionTheSourceHas) {
  EXPECT_EQ(repairSentence("No! No!", "No! No!"), "No! No!");
  EXPECT_EQ(repairSentence("Help! Help!", "Aiuto! Aiuto!"), "Aiuto! Aiuto!");
}

TEST(SentenceRepair, KeepsDifferentSentences) {
  EXPECT_EQ(repairSentence("Guys? Guys, I...", "Ragazzi? Ragazzi, io..."), "Ragazzi? Ragazzi, io...");
  EXPECT_EQ(repairSentence("Okay. Let's go.", "Ok. Andiamo."), "Ok. Andiamo.");
  EXPECT_EQ(repairSentence("What is it?", "Che cos'è?"), "Che cos'è?");
}

TEST(SentenceRepair, DropsADashTheSourceHasNot) {
  EXPECT_EQ(repairSentence("No.", "- No."), "No.");
  EXPECT_EQ(repairSentence("What?", "- Cosa? Cosa?"), "Cosa?");
}

TEST(SentenceRepair, KeepsDashesTheSourceHasOrADialogue) {
  EXPECT_EQ(repairSentence("- No.", "- No."), "- No.");
  EXPECT_EQ(repairSentence("Yes. No.", "- Sì. - No."), "- Sì. - No.");
}

TEST(SentenceRepair, LeavesEmptyAndTaggedTextAlone) {
  EXPECT_EQ(repairSentence("", ""), "");
  EXPECT_EQ(repairSentence(" ", " "), " ");
  EXPECT_EQ(repairSentence("<i>What?</i>", "<i>Cosa?</i>"), "<i>Cosa?</i>");
}
