#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "test_helpers.hpp"

using subtitle_engine::Translator;
using subtitle_engine::TranslatorSpec;

namespace {

std::unique_ptr<Translator> openPairs(std::vector<std::string> pairs) {
  TranslatorSpec spec;
  for (const auto& p : pairs) spec.models.push_back(modelFiles(p));
  std::string error;
  auto translator = Translator::open(spec, &error);
  EXPECT_TRUE(translator) << error;
  return translator;
}

}  // namespace

TEST(Translator, TranslatesHelloWorld) {
  auto t = openPairs({"enit"});
  auto out = t->translate({"Hello world, how are you?"});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_NE(out[0].find("Ciao"), std::string::npos) << out[0];
}

TEST(Translator, KeepsCountAndOrder) {
  auto t = openPairs({"enit"});
  std::vector<std::string> lines;
  for (int i = 0; i < 50; ++i) lines.push_back("This is sentence number " + std::to_string(i) + ".");
  auto out = t->translate(lines);
  ASSERT_EQ(out.size(), 50u);
  EXPECT_EQ(t->translate({lines[7]})[0], out[7]);
}

TEST(Translator, KeepsItalicsAndLineBreaks) {
  auto t = openPairs({"enit"});
  auto out = t->translate({"<i>Nobody believed him.</i>", "I told you, I was at the station\nwaiting for the last train."});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].rfind("<i>", 0), 0u) << out[0];
  EXPECT_EQ(out[0].substr(out[0].size() - 4), "</i>") << out[0];
  EXPECT_EQ(std::count(out[1].begin(), out[1].end(), '\n'), 1) << out[1];
  EXPECT_EQ(out[1].find("<br>"), std::string::npos) << out[1];
}

TEST(Translator, KeepsEmptyAndTagOnlyLinesInPlace) {
  auto t = openPairs({"enit"});
  auto out = t->translate({"A.", "", "<br>", "<i></i>", "B."});
  ASSERT_EQ(out.size(), 5u);
  EXPECT_EQ(out[1], "");
}

TEST(Translator, PivotsSpanishToItalian) {
  auto t = openPairs({"esen", "enit"});
  auto out = t->translate({"¿Dónde está la estación de tren?"});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_NE(out[0].find("stazione"), std::string::npos) << out[0];
}

TEST(Translator, TranslatesWithSeparateVocabularies) {
  const std::string dir = modelDir("zh-Hanten");
  TranslatorSpec spec;
  subtitle_engine::ModelFiles files;
  files.model = dir + "/model.zhen.intgemm.alphas.bin";
  files.vocab = dir + "/srcvocab.zhen.spm";
  files.targetVocab = dir + "/trgvocab.zhen.spm";
  files.lex = dir + "/lex.50.50.zhen.s2t.bin";
  spec.models.push_back(files);
  std::string error;
  auto t = Translator::open(spec, &error);
  ASSERT_TRUE(t) << error;
  auto out = t->translate({"你好，世界。"});
  ASSERT_EQ(out.size(), 1u);
  std::string lower = out[0];
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  EXPECT_NE(lower.find("hello"), std::string::npos) << out[0];
}
