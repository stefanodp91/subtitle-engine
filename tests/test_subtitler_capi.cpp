#include <gtest/gtest.h>
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "subtitle_engine_c.h"
#include "test_helpers.hpp"

using nlohmann::json;

namespace {

std::string fixture(const std::string& name) { return std::string(SE_FIXTURES) + "/" + name; }
bool exists(const std::string& path) { return std::ifstream(path).good(); }

json takeJson(char* s) {
  json j = json::parse(s);
  se_free(s);
  return j;
}

std::string errorCode(char* error) {
  if (!error) return "";
  json j = takeJson(error);
  return j.at("code").get<std::string>();
}

// A media source in plain C over a file; failing: every read is an error.
struct CFile {
  std::FILE* file = nullptr;
  bool failing = false;
};

long long readFile(void* opaque, unsigned long long offset, unsigned char* buffer, unsigned long long length) {
  auto* f = static_cast<CFile*>(opaque);
  if (f->failing) return SE_READ_ERROR;
  std::fseek(f->file, static_cast<long>(offset), SEEK_SET);
  const size_t got = std::fread(buffer, 1, static_cast<size_t>(length), f->file);
  return got > 0 ? static_cast<long long>(got) : SE_READ_END;
}

se_media_source sourceOver(CFile* f, const std::string& path) {
  f->file = std::fopen(path.c_str(), "rb");
  std::fseek(f->file, 0, SEEK_END);
  const long size = std::ftell(f->file);
  return se_media_source{f, readFile, size};
}

std::string listenerSpec() {
  return json{{"model", fixture("whisper/ggml-base-q5_1.bin")},
              {"vad", fixture("whisper/ggml-silero-v6.2.0.bin")},
              {"alignment_heads", "base"}}
      .dump();
}

std::string translatorSpec() {
  auto f = modelFiles("enit");
  return json{{"models", json::array({{{"model", f.model}, {"vocab", f.vocab}, {"lex", f.lex}}})}}.dump();
}

bool haveSpeech() {
  return exists(fixture("whisper/ggml-base-q5_1.bin")) && exists(fixture("speech/english.mkv")) &&
         exists(modelFiles("enit").model);
}

}  // namespace

TEST(ListenerCapi, ReportsMissingModel) {
  char* error = nullptr;
  json spec{{"model", fixture("whisper/no-such-model.bin")}, {"vad", fixture("whisper/ggml-silero-v6.2.0.bin")}};
  EXPECT_EQ(se_listener_open(spec.dump().c_str(), &error), nullptr);
  EXPECT_EQ(errorCode(error), "model_load_failed");
}

TEST(SubtitlerCapi, RoundTripsOnFixture) {
  if (!haveSpeech()) GTEST_SKIP() << "run scripts/fetch_test_models.sh and scripts/make_speech_fixtures.sh";
  char* error = nullptr;
  se_listener* listener = se_listener_open(listenerSpec().c_str(), &error);
  ASSERT_NE(listener, nullptr) << errorCode(error);
  se_translator* translator = se_translator_open(translatorSpec().c_str(), &error);
  ASSERT_NE(translator, nullptr) << errorCode(error);
  CFile f;
  se_media_source source = sourceOver(&f, fixture("speech/english.mkv"));
  se_subtitler* sub = se_subtitler_open(listener, &source, R"({"target_language":"it"})", nullptr, nullptr, &error);
  ASSERT_NE(sub, nullptr) << errorCode(error);
  ASSERT_EQ(se_subtitler_set_translator(sub, translator, &error), 1);
  char* step = se_subtitler_step(sub, &error);
  ASSERT_NE(step, nullptr) << errorCode(error);
  json j = takeJson(step);
  EXPECT_EQ(j.at("status"), "cues");
  EXPECT_EQ(j.at("audio_language"), "en");
  bool found = false;
  for (const auto& c : j.at("cues")) found = found || c.at("text").get<std::string>().find("stazione") != std::string::npos;
  EXPECT_TRUE(found) << j.dump();
  se_subtitler_close(sub);
  se_translator_close(translator);
  se_listener_close(listener);
  std::fclose(f.file);
}

TEST(SubtitlerCapi, RejectsMalformedSpec) {
  if (!haveSpeech()) GTEST_SKIP() << "run scripts/fetch_test_models.sh and scripts/make_speech_fixtures.sh";
  char* error = nullptr;
  se_listener* listener = se_listener_open(listenerSpec().c_str(), &error);
  ASSERT_NE(listener, nullptr) << errorCode(error);
  CFile f;
  se_media_source source = sourceOver(&f, fixture("audio_ac3.mkv"));
  EXPECT_EQ(se_subtitler_open(listener, &source, "{", nullptr, nullptr, &error), nullptr);
  EXPECT_EQ(errorCode(error), "invalid_argument");
  error = nullptr;
  EXPECT_EQ(se_subtitler_open(listener, nullptr, R"({"target_language":"it"})", nullptr, nullptr, &error), nullptr);
  EXPECT_EQ(errorCode(error), "invalid_argument");
  error = nullptr;
  se_media_source noRead{&f, nullptr, source.size};
  EXPECT_EQ(se_subtitler_open(listener, &noRead, R"({"target_language":"it"})", nullptr, nullptr, &error), nullptr);
  EXPECT_EQ(errorCode(error), "invalid_argument");
  se_listener_close(listener);
  std::fclose(f.file);
}

TEST(SubtitlerCapi, ReadErrorReported) {
  if (!haveSpeech()) GTEST_SKIP() << "run scripts/fetch_test_models.sh and scripts/make_speech_fixtures.sh";
  char* error = nullptr;
  se_listener* listener = se_listener_open(listenerSpec().c_str(), &error);
  ASSERT_NE(listener, nullptr) << errorCode(error);
  CFile f;
  const std::string path = fixture("audio_ac3.mkv");
  se_media_source source = sourceOver(&f, path);
  f.failing = true;
  EXPECT_EQ(se_subtitler_open(listener, &source, R"({"target_language":"it"})", nullptr, nullptr, &error), nullptr);
  ASSERT_NE(error, nullptr);
  json j = takeJson(error);
  EXPECT_EQ(j.at("code"), "io_error");
  EXPECT_EQ(j.at("message").get<std::string>().find(SE_FIXTURES), std::string::npos);
  se_listener_close(listener);
  std::fclose(f.file);
}

TEST(SubtitlerCapi, LoadsSavedSubtitles) {
  if (!haveSpeech()) GTEST_SKIP() << "run scripts/fetch_test_models.sh and scripts/make_speech_fixtures.sh";
  const std::string dir = std::string(SE_TMP) + "/capi_store";
  ::mkdir(dir.c_str(), 0755);
  const std::string path = dir + "/english.json";
  std::remove(path.c_str());
  char* error = nullptr;
  se_listener* listener = se_listener_open(listenerSpec().c_str(), &error);
  ASSERT_NE(listener, nullptr) << errorCode(error);
  se_translator* translator = se_translator_open(translatorSpec().c_str(), &error);
  ASSERT_NE(translator, nullptr) << errorCode(error);
  CFile f;
  se_media_source source = sourceOver(&f, fixture("speech/english.mkv"));
  json spec{{"target_language", "it"}, {"store", {{"path", path}, {"media_id", "english-v1"}}}};
  se_subtitler* sub = se_subtitler_open(listener, &source, spec.dump().c_str(), nullptr, nullptr, &error);
  ASSERT_NE(sub, nullptr) << errorCode(error);
  se_subtitler_set_translator(sub, translator, &error);
  json given = json::array();
  for (int i = 0; i < 2; ++i) {
    json step = takeJson(se_subtitler_step(sub, &error));
    for (const auto& c : step.at("cues")) given.push_back(c);
  }
  se_subtitler_close(sub);
  char* loaded = se_subtitles_load(path.c_str(), "english-v1", "it", &error);
  ASSERT_NE(loaded, nullptr) << errorCode(error);
  EXPECT_EQ(takeJson(loaded).at("cues"), given);
  EXPECT_EQ(se_subtitles_load((dir + "/missing.json").c_str(), "english-v1", "it", &error), nullptr);
  EXPECT_EQ(errorCode(error), "not_found");
  se_translator_close(translator);
  se_listener_close(listener);
  std::fclose(f.file);
}
