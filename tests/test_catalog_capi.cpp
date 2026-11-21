#include "model_store_fixture.hpp"

namespace {

json toJson(const ModelEntry& m) {
  json files = json::array();
  for (const auto& f : m.files) {
    files.push_back({{"role", f.role},
                     {"name", f.name},
                     {"url", f.url},
                     {"compression", f.compression == Compression::kZstd ? "zstd" : "none"},
                     {"size", f.size},
                     {"sha256", f.sha256},
                     {"installed_size", f.installedSize},
                     {"installed_sha256", f.installedSha256}});
  }
  return {{"id", m.id},           {"kind", "translator"}, {"source_language", m.sourceLanguage},
          {"target_language", m.targetLanguage}, {"version", m.version}, {"files", files}};
}

ModelEntry esen() {
  ModelEntry m = enit();
  m.id = "esen";
  m.sourceLanguage = "es";
  m.targetLanguage = "en";
  return m;
}

std::string catalogText(const std::vector<ModelEntry>& models) {
  json j = {{"format", 1}, {"pivot_language", "en"}, {"models", json::array()}};
  for (const auto& m : models) j["models"].push_back(toJson(m));
  return j.dump();
}

json take(char* out) {
  EXPECT_NE(out, nullptr);
  if (!out) return nullptr;
  json j = json::parse(out);
  se_free(out);
  return j;
}

// The error's code; the message must never carry the models directory.
std::string codeOf(char* error, const std::string& dir = "") {
  if (!error) return "";
  json j = json::parse(error);
  se_free(error);
  if (!dir.empty()) EXPECT_EQ(j["message"].get<std::string>().find(dir), std::string::npos);
  return j["code"].get<std::string>();
}

struct Catalog {
  explicit Catalog(const std::string& text) {
    char* error = nullptr;
    handle = se_catalog_open(text.c_str(), &error);
    EXPECT_NE(handle, nullptr) << (error ? error : "");
    se_free(error);
  }
  ~Catalog() { se_catalog_close(handle); }
  se_catalog* handle = nullptr;
};

// Saves what se_model_downloads asks for, as a downloader would.
void download(se_catalog* catalog, const std::string& dir, const ModelEntry& m) {
  char* error = nullptr;
  json targets = take(se_model_downloads(catalog, dir.c_str(), m.id.c_str(), &error));
  ASSERT_EQ(targets.size(), m.files.size());
  for (size_t i = 0; i < m.files.size(); ++i) {
    const auto& plain = kPlain.at(m.files[i].name);
    writeAll(targets[i]["path"], m.files[i].compression == Compression::kZstd ? zstd(plain) : plain);
    EXPECT_EQ(targets[i]["size"], m.files[i].size);
    EXPECT_EQ(targets[i]["url"], m.files[i].url);
  }
}

}  // namespace

TEST(CatalogCapi, OpenBadJsonIsInvalidArgument) {
  char* error = nullptr;
  EXPECT_EQ(se_catalog_open("{not json", &error), nullptr);
  EXPECT_EQ(codeOf(error), "invalid_argument");
  EXPECT_EQ(se_catalog_open(nullptr, &error), nullptr);
  EXPECT_EQ(codeOf(error), "invalid_argument");
}

TEST(CatalogCapi, OpenFormat2IsUnsupported) {
  json j = json::parse(catalogText({enit()}));
  j["format"] = 2;
  char* error = nullptr;
  EXPECT_EQ(se_catalog_open(j.dump().c_str(), &error), nullptr);
  EXPECT_EQ(codeOf(error), "unsupported_format");
}

TEST(CatalogCapi, ModelsListsSizes) {
  Catalog c(catalogText({enit(), esen()}));
  char* error = nullptr;
  json models = take(se_catalog_models(c.handle, &error));
  ASSERT_EQ(models.size(), 2u);
  EXPECT_EQ(models[0]["id"], "enit");
  EXPECT_EQ(models[0]["kind"], "translator");
  EXPECT_EQ(models[0]["source_language"], "en");
  EXPECT_EQ(models[0]["version"], "3.0");
  EXPECT_EQ(models[0]["download_bytes"], enit().downloadBytes());
  EXPECT_EQ(models[0]["installed_bytes"], enit().installedBytes());
}

TEST(CatalogCapi, ChainJson) {
  Catalog c(catalogText({enit(), esen()}));
  char* error = nullptr;
  EXPECT_EQ(take(se_catalog_chain(c.handle, "es", "it", &error)), (json{"esen", "enit"}));
  EXPECT_EQ(take(se_catalog_chain(c.handle, "it", "it", &error)), json::array());
  EXPECT_EQ(se_catalog_chain(c.handle, "xx", "it", &error), nullptr);
  EXPECT_EQ(codeOf(error), "no_chain");
}

TEST(CatalogCapi, CheckAllAndSome) {
  const std::string dir = freshDir("capi_check");
  Catalog c(catalogText({enit(), esen()}));
  char* error = nullptr;
  json all = take(se_models_check(c.handle, dir.c_str(), nullptr, "quick", &error));
  EXPECT_EQ(all, (json{{{"id", "enit"}, {"status", "missing"}}, {{"id", "esen"}, {"status", "missing"}}}));
  json one = take(se_models_check(c.handle, dir.c_str(), R"(["esen"])", "full", &error));
  EXPECT_EQ(one, (json{{{"id", "esen"}, {"status", "missing"}}}));
  EXPECT_EQ(se_models_check(c.handle, dir.c_str(), nullptr, "slow", &error), nullptr);
  EXPECT_EQ(codeOf(error, dir), "invalid_argument");
  EXPECT_EQ(se_models_check(c.handle, dir.c_str(), R"(["nope"])", "quick", &error), nullptr);
  EXPECT_EQ(codeOf(error, dir), "unknown_model");
  EXPECT_EQ(se_models_check(c.handle, dir.c_str(), R"({"id":"enit"})", "quick", &error), nullptr);
  EXPECT_EQ(codeOf(error, dir), "invalid_argument");
}

TEST(CatalogCapi, DownloadsInstallCheck) {
  const std::string dir = freshDir("capi_install");
  Catalog c(catalogText({enit(), esen()}));
  char* error = nullptr;
  EXPECT_EQ(se_model_install(c.handle, dir.c_str(), "enit", &error), 0);
  EXPECT_EQ(codeOf(error, dir), "download_missing");
  download(c.handle, dir, enit());
  EXPECT_EQ(se_model_install(c.handle, dir.c_str(), "enit", &error), 1) << (error ? error : "");
  json status = take(se_models_check(c.handle, dir.c_str(), R"(["enit"])", "full", &error));
  EXPECT_EQ(status[0]["status"], "installed");
  EXPECT_EQ(se_model_install(c.handle, dir.c_str(), "nope", &error), 0);
  EXPECT_EQ(codeOf(error, dir), "unknown_model");
}

TEST(CatalogCapi, PathsAndTranslatorSpec) {
  const std::string dir = freshDir("capi_paths");
  Catalog c(catalogText({enit(), esen()}));
  char* error = nullptr;
  EXPECT_EQ(se_model_paths(c.handle, dir.c_str(), "enit", &error), nullptr);
  EXPECT_EQ(codeOf(error, dir), "not_installed");
  download(c.handle, dir, enit());
  ASSERT_EQ(se_model_install(c.handle, dir.c_str(), "enit", &error), 1);

  json paths = take(se_model_paths(c.handle, dir.c_str(), "enit", &error));
  EXPECT_EQ(paths, (json{{"model", dir + "/enit/model.enit.bin"},
                         {"vocab", dir + "/enit/vocab.enit.spm"},
                         {"lex", dir + "/enit/lex.enit.bin"}}));
  json spec = take(se_catalog_translator_spec(c.handle, dir.c_str(), "en", "it", &error));
  ASSERT_EQ(spec["models"].size(), 1u);
  EXPECT_EQ(spec["models"][0]["model"], dir + "/enit/model.enit.bin");
  EXPECT_EQ(se_catalog_translator_spec(c.handle, dir.c_str(), "es", "it", &error), nullptr);
  EXPECT_EQ(codeOf(error, dir), "not_installed");
  EXPECT_EQ(se_catalog_translator_spec(c.handle, dir.c_str(), "xx", "it", &error), nullptr);
  EXPECT_EQ(codeOf(error, dir), "no_chain");
}

TEST(CatalogCapi, TranslatorSpecOpens) {
  const auto files = modelFiles("enit");
  if (!fs::exists(files.model)) GTEST_SKIP() << "test models missing: scripts/fetch_test_models.sh";
  ModelEntry m;
  m.id = "enit";
  m.sourceLanguage = "en";
  m.targetLanguage = "it";
  m.version = "3.0";
  for (const auto& [role, path] : std::map<std::string, std::string>{
           {"model", files.model}, {"vocab", files.vocab}, {"lex", files.lex}}) {
    std::string error;
    const std::string hash = subtitle_engine::sha256File(path, &error);
    const uint64_t size = fs::file_size(path);
    m.files.push_back({role, fs::path(path).filename().string(), "https://example.org/x", Compression::kNone, size,
                       size, hash, hash});
  }
  const std::string dir = freshDir("capi_real");
  Catalog c(catalogText({m}));
  char* error = nullptr;
  json targets = take(se_model_downloads(c.handle, dir.c_str(), "enit", &error));
  for (size_t i = 0; i < m.files.size(); ++i) {
    fs::copy_file(modelDir("enit") + "/" + m.files[i].name, targets[i]["path"].get<std::string>());
  }
  ASSERT_EQ(se_model_install(c.handle, dir.c_str(), "enit", &error), 1) << (error ? error : "");
  char* spec = se_catalog_translator_spec(c.handle, dir.c_str(), "en", "it", &error);
  ASSERT_NE(spec, nullptr);
  se_translator* t = se_translator_open(spec, &error);
  se_free(spec);
  ASSERT_NE(t, nullptr) << (error ? error : "");
  json out = take(se_translate(t, R"(["Good morning."])", &error));
  EXPECT_FALSE(out[0].get<std::string>().empty());
  se_translator_close(t);
}
