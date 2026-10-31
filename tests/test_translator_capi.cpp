#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "subtitle_engine_c.h"
#include "test_helpers.hpp"

using nlohmann::json;

namespace {

std::string specFor(std::vector<std::string> pairs) {
  json spec{{"models", json::array()}};
  for (const auto& p : pairs) {
    auto f = modelFiles(p);
    spec["models"].push_back({{"model", f.model}, {"vocab", f.vocab}, {"lex", f.lex}});
  }
  return spec.dump();
}

json takeJson(char* s) {
  json j = json::parse(s);
  se_free(s);
  return j;
}

}  // namespace

TEST(TranslatorCapi, RoundTripsJson) {
  char* error = nullptr;
  se_translator* t = se_translator_open(specFor({"enit"}).c_str(), &error);
  ASSERT_NE(t, nullptr) << (error ? error : "");
  char* out = se_translate(t, R"(["Hello world, how are you?"])", &error);
  ASSERT_NE(out, nullptr) << (error ? error : "");
  json result = takeJson(out);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_NE(result[0].get<std::string>().find("Ciao"), std::string::npos);
  se_translator_close(t);
}

TEST(TranslatorCapi, RejectsMalformedSpec) {
  for (const char* spec : {"{", R"({"models":[]})", R"({"models":[{},{},{}]})"}) {
    char* error = nullptr;
    EXPECT_EQ(se_translator_open(spec, &error), nullptr) << spec;
    ASSERT_NE(error, nullptr) << spec;
    EXPECT_EQ(takeJson(error)["code"], "invalid_argument") << spec;
  }
}

TEST(TranslatorCapi, ReportsMissingModel) {
  char* error = nullptr;
  const std::string spec = R"({"models":[{"model":"/nope/model.bin","vocab":"/nope/vocab.spm","lex":"/nope/lex.bin"}]})";
  EXPECT_EQ(se_translator_open(spec.c_str(), &error), nullptr);
  ASSERT_NE(error, nullptr);
  json e = takeJson(error);
  EXPECT_EQ(e["code"], "model_load_failed");
  EXPECT_FALSE(e["message"].get<std::string>().empty());
}

TEST(TranslatorCapi, AcceptsTargetVocab) {
  const std::string dir = modelDir("zh-Hanten");
  json spec{{"models", json::array({{{"model", dir + "/model.zhen.intgemm.alphas.bin"},
                                      {"vocab", dir + "/srcvocab.zhen.spm"},
                                      {"target_vocab", dir + "/trgvocab.zhen.spm"},
                                      {"lex", dir + "/lex.50.50.zhen.s2t.bin"}}})}};
  char* error = nullptr;
  se_translator* t = se_translator_open(spec.dump().c_str(), &error);
  ASSERT_NE(t, nullptr) << (error ? error : "");
  char* out = se_translate(t, R"(["你好，世界。"])", &error);
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(takeJson(out).size(), 1u);
  se_translator_close(t);
}
