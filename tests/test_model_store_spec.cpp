#include "model_store_fixture.hpp"

TEST(ModelStore, TranslatorSpecForSplitVocab) {
  const std::string dir = freshDir("spec");
  ModelEntry split;
  split.id = "zhen";
  split.kind = ModelKind::kTranslator;
  split.version = "3.0";
  split.files = {fileOf("model", "m.bin", "m", Compression::kNone), fileOf("srcvocab", "s.spm", "s", Compression::kNone),
                 fileOf("trgvocab", "t.spm", "t", Compression::kNone), fileOf("lex", "l.bin", "l", Compression::kNone)};
  fs::create_directories(dir + "/zhen");
  for (const auto& f : split.files) writeAll(dir + "/zhen/" + f.name, f.name.substr(0, 1));
  writeAll(dir + "/zhen/installed.json", json{{"version", "3.0"}}.dump());
  writeOtherWriterLayout(dir, enit());
  ModelStore store(dir);

  ModelEntry e = enit();
  auto spec = store.translatorSpec({&split, &e});
  ASSERT_TRUE(spec.has_value());
  json j = json::parse(*spec);
  ASSERT_EQ(j["models"].size(), 2u);
  EXPECT_EQ(j["models"][0]["vocab"], dir + "/zhen/s.spm");
  EXPECT_EQ(j["models"][0]["target_vocab"], dir + "/zhen/t.spm");
  EXPECT_EQ(j["models"][0]["model"], dir + "/zhen/m.bin");
  EXPECT_EQ(j["models"][0]["lex"], dir + "/zhen/l.bin");
  EXPECT_FALSE(j["models"][1].contains("target_vocab"));
  EXPECT_EQ(j["models"][1]["vocab"], dir + "/enit/vocab.enit.spm");

  fs::remove(dir + "/enit/installed.json");
  EXPECT_FALSE(store.translatorSpec({&e}).has_value());
}

TEST(ModelStore, InstalledTranslatorOpens) {
  const auto files = modelFiles("enit");
  if (!fs::exists(files.model)) GTEST_SKIP() << "test models missing: scripts/fetch_test_models.sh";
  const std::string dir = freshDir("real");
  ModelEntry m;
  m.id = "enit";
  m.kind = ModelKind::kTranslator;
  m.version = "3.0";
  for (const auto& [role, path] : std::map<std::string, std::string>{
           {"model", files.model}, {"vocab", files.vocab}, {"lex", files.lex}}) {
    std::string error;
    const std::string hash = subtitle_engine::sha256File(path, &error);
    const uint64_t size = fs::file_size(path);
    m.files.push_back({role, fs::path(path).filename().string(), "https://example.org/x", Compression::kNone, size,
                       size, hash, hash});
  }
  ModelStore store(dir);
  for (size_t i = 0; i < m.files.size(); ++i) {
    const std::string from = modelDir("enit") + "/" + m.files[i].name;
    fs::copy_file(from, store.downloads(m)[i].path, fs::copy_options::overwrite_existing);
  }
  ASSERT_EQ(store.install(m), "");
  auto spec = store.translatorSpec({&m});
  ASSERT_TRUE(spec.has_value());
  char* error = nullptr;
  se_translator* t = se_translator_open(spec->c_str(), &error);
  ASSERT_NE(t, nullptr) << (error ? error : "");
  char* out = se_translate(t, R"(["Good morning."])", &error);
  ASSERT_NE(out, nullptr);
  EXPECT_FALSE(json::parse(out)[0].get<std::string>().empty());
  se_free(out);
  se_translator_close(t);
}
