#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "subtitle_engine/model_catalog.hpp"

using nlohmann::json;
using subtitle_engine::Compression;
using subtitle_engine::ModelCatalog;
using subtitle_engine::ModelKind;

namespace {

const std::string kHash(64, 'a');

json file(const std::string& role, const std::string& name, const std::string& compression = "zstd") {
  return {{"role", role},           {"name", name},           {"url", "https://example.org/" + name},
          {"compression", compression}, {"size", 10},          {"sha256", kHash},
          {"installed_size", 20},   {"installed_sha256", kHash}};
}

json translator(const std::string& source, const std::string& target, bool splitVocab = false) {
  const std::string id = source + target;
  json files = json::array({file("model", "model." + id + ".bin"), file("lex", "lex." + id + ".bin")});
  if (splitVocab) {
    files.push_back(file("srcvocab", "srcvocab." + id + ".spm"));
    files.push_back(file("trgvocab", "trgvocab." + id + ".spm"));
  } else {
    files.push_back(file("vocab", "vocab." + id + ".spm"));
  }
  return {{"id", id},       {"kind", "translator"},       {"source_language", source}, {"target_language", target},
          {"version", "3.0"}, {"architecture", "base-memory"}, {"files", files}};
}

json catalog(json models) { return {{"format", 1}, {"pivot_language", "en"}, {"models", std::move(models)}}; }

json sample() {
  return catalog(json::array(
      {translator("en", "it"), translator("es", "en"), translator("zh-Hant", "en", true),
       {{"id", "whisper-small"}, {"kind", "speech"}, {"version", "1"}, {"alignment_heads", "small"},
        {"files", json::array({file("model", "ggml-small.bin", "none")})}},
       {{"id", "vad"}, {"kind", "voice_detector"}, {"version", "6.2.0"},
        {"files", json::array({file("model", "ggml-vad.bin", "none")})}}}));
}

std::string parseError(const json& j) {
  std::string error;
  EXPECT_FALSE(ModelCatalog::parse(j.dump(), &error).has_value());
  return error;
}

std::vector<std::string> ids(const std::vector<const subtitle_engine::ModelEntry*>& chain) {
  std::vector<std::string> out;
  for (const auto* m : chain) out.push_back(m->id);
  return out;
}

}  // namespace

TEST(ModelCatalog, ParsesTranslatorsAndSpeech) {
  std::string error;
  auto parsed = ModelCatalog::parse(sample().dump(), &error);
  ASSERT_TRUE(parsed.has_value()) << error;
  EXPECT_EQ(parsed->models().size(), 5u);
  const auto* enit = parsed->find("enit");
  ASSERT_NE(enit, nullptr);
  EXPECT_EQ(enit->kind, ModelKind::kTranslator);
  EXPECT_EQ(enit->sourceLanguage, "en");
  EXPECT_EQ(enit->targetLanguage, "it");
  EXPECT_EQ(enit->version, "3.0");
  EXPECT_EQ(enit->architecture, "base-memory");
  ASSERT_EQ(enit->files.size(), 3u);
  EXPECT_EQ(enit->files[0].compression, Compression::kZstd);
  EXPECT_EQ(enit->files[0].installedSize, 20u);
  EXPECT_EQ(parsed->find("zh-Hanten")->files.size(), 4u);
  const auto* speech = parsed->find("whisper-small");
  ASSERT_NE(speech, nullptr);
  EXPECT_EQ(speech->kind, ModelKind::kSpeech);
  EXPECT_EQ(speech->alignmentHeads, "small");
  EXPECT_EQ(speech->files[0].compression, Compression::kNone);
  EXPECT_EQ(parsed->find("vad")->kind, ModelKind::kVoiceDetector);
  EXPECT_EQ(parsed->find("nothing"), nullptr);
}

TEST(ModelCatalog, RejectsUnknownFormat) {
  json j = sample();
  j["format"] = 2;
  EXPECT_EQ(parseError(j), "unsupported_format");
}

TEST(ModelCatalog, RejectsBadEntries) {
  std::string error;
  EXPECT_FALSE(ModelCatalog::parse("{not json", &error).has_value());
  EXPECT_EQ(error, "invalid_argument");

  auto withFile = [](const std::string& key, json value) {
    json j = sample();
    j["models"][0]["files"][0][key] = std::move(value);
    return j;
  };
  EXPECT_EQ(parseError(withFile("sha256", std::string(63, 'a'))), "invalid_argument");
  EXPECT_EQ(parseError(withFile("sha256", std::string(64, 'g'))), "invalid_argument");
  EXPECT_EQ(parseError(withFile("installed_sha256", std::string(64, 'A'))), "invalid_argument");
  EXPECT_EQ(parseError(withFile("size", 0)), "invalid_argument");
  EXPECT_EQ(parseError(withFile("compression", "gzip")), "invalid_argument");
  EXPECT_EQ(parseError(withFile("url", "http://example.org/x")), "invalid_argument");

  json duplicate = sample();
  duplicate["models"].push_back(translator("en", "it"));
  EXPECT_EQ(parseError(duplicate), "invalid_argument");

  json unknownKind = sample();
  unknownKind["models"][0]["kind"] = "painter";
  EXPECT_EQ(parseError(unknownKind), "invalid_argument");

  json noLex = sample();
  noLex["models"][0]["files"].erase(1);
  EXPECT_EQ(parseError(noLex), "invalid_argument");

  json bothVocabs = sample();
  bothVocabs["models"][0]["files"].push_back(file("srcvocab", "srcvocab.enit.spm"));
  EXPECT_EQ(parseError(bothVocabs), "invalid_argument");

  json noFormat = sample();
  noFormat.erase("format");
  EXPECT_EQ(parseError(noFormat), "invalid_argument");
}

TEST(ModelCatalog, RejectsPathsInNames) {
  for (const std::string bad : {"../x", "a/b", "a\\b", "", ".hidden", "..", "a..b"}) {
    json badName = sample();
    badName["models"][0]["files"][0]["name"] = bad;
    EXPECT_EQ(parseError(badName), "invalid_argument") << "name " << bad;
    json badId = sample();
    badId["models"][0]["id"] = bad;
    EXPECT_EQ(parseError(badId), "invalid_argument") << "id " << bad;
  }
}

TEST(ModelCatalog, Chain) {
  std::string error;
  auto parsed = ModelCatalog::parse(sample().dump(), &error);
  ASSERT_TRUE(parsed.has_value()) << error;
  EXPECT_EQ(ids(*parsed->chain("en", "it")), (std::vector<std::string>{"enit"}));
  EXPECT_EQ(ids(*parsed->chain("es", "it")), (std::vector<std::string>{"esen", "enit"}));
  EXPECT_TRUE(parsed->chain("it", "it")->empty());
  EXPECT_FALSE(parsed->chain("xx", "it").has_value());
  EXPECT_FALSE(parsed->chain("en", "xx").has_value());
  EXPECT_FALSE(parsed->chain("it", "en").has_value());

  json noEsen = sample();
  noEsen["models"].erase(1);
  auto without = ModelCatalog::parse(noEsen.dump(), &error);
  ASSERT_TRUE(without.has_value()) << error;
  EXPECT_FALSE(without->chain("es", "it").has_value());
}

TEST(ModelCatalog, SizesAddUp) {
  std::string error;
  auto parsed = ModelCatalog::parse(sample().dump(), &error);
  ASSERT_TRUE(parsed.has_value()) << error;
  EXPECT_EQ(parsed->find("enit")->downloadBytes(), 30u);
  EXPECT_EQ(parsed->find("enit")->installedBytes(), 60u);
  EXPECT_EQ(parsed->find("zh-Hanten")->downloadBytes(), 40u);
}

TEST(ShippedCatalog, ParsesAndHasTheModels) {
  std::ifstream in(std::string(SE_CATALOG) + "/models.json", std::ios::binary);
  ASSERT_TRUE(in.good());
  std::string text((std::istreambuf_iterator<char>(in)), {});
  std::string error;
  auto parsed = ModelCatalog::parse(text, &error);
  ASSERT_TRUE(parsed.has_value()) << error;
  for (const char* id : {"enit", "esen", "iten", "whisper-small-q6_k", "silero-v6.2.0"}) {
    EXPECT_NE(parsed->find(id), nullptr) << id;
  }
  EXPECT_EQ(parsed->find("whisper-small-q6_k")->alignmentHeads, "small");
  for (const auto& model : parsed->models()) {
    for (const auto& f : model.files) {
      if (model.kind == ModelKind::kTranslator) {
        EXPECT_EQ(f.url.rfind("https://firefox-settings-attachments.cdn.mozilla.net/", 0), 0u) << model.id;
      }
    }
  }
  EXPECT_EQ(parsed->find("whisper-small-q6_k")->files[0].url.rfind(
                "https://github.com/stefanodp91/subtitle-engine/releases/download/", 0),
            0u);
}
