#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <string>

#include "file_source.hpp"
#include "subtitle_engine/listener.hpp"
#include "subtitle_engine/subtitler.hpp"
#include "subtitle_engine/translator.hpp"
#include "test_helpers.hpp"

using namespace subtitle_engine;

namespace {
std::string fixture(const std::string& name) { return std::string(SE_FIXTURES) + "/" + name; }
}  // namespace

TEST(SubtitlerIntegration, EnglishToItalian) {
  const std::string model = fixture("whisper/ggml-base-q5_1.bin"), film = fixture("speech/english.mkv");
  if (!std::ifstream(model).good() || !std::ifstream(film).good() || !std::ifstream(modelFiles("enit").model).good())
    GTEST_SKIP() << "run scripts/fetch_test_models.sh and scripts/make_speech_fixtures.sh";
  ListenerSpec ls;
  ls.modelPath = model;
  ls.vadPath = fixture("whisper/ggml-silero-v6.2.0.bin");
  ls.heads = AlignmentHeads::kBase;
  std::string error;
  auto listener = Listener::open(ls, &error);
  ASSERT_TRUE(listener) << error;
  auto translator = Translator::open({{modelFiles("enit")}}, &error);
  ASSERT_TRUE(translator) << error;
  FileSource source(film);
  SubtitlerSpec spec;
  spec.targetLanguage = "it";
  auto sub = Subtitler::open(*listener, source, spec, nullptr, &error);
  ASSERT_TRUE(sub) << error;
  sub->setTranslator(translator.get());
  Step first = sub->step();
  ASSERT_EQ(first.status, StepStatus::kCues);
  bool found = false;
  for (const auto& c : first.cues) found = found || c.text.find("stazione") != std::string::npos;
  EXPECT_TRUE(found);
}
