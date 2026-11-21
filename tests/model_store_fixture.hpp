#pragma once
// Helpers of the model store tests: a small translator with known files.
#include <gtest/gtest.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <zstd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "subtitle_engine/model_catalog.hpp"
#include "subtitle_engine/model_store.hpp"
#include "subtitle_engine/sha256_file.hpp"
#include "subtitle_engine_c.h"
#include "test_helpers.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using subtitle_engine::CheckMode;
using subtitle_engine::Compression;
using subtitle_engine::ModelEntry;
using subtitle_engine::ModelFile;
using subtitle_engine::ModelKind;
using subtitle_engine::ModelStatus;
using subtitle_engine::ModelStore;

inline const std::string kWeights = std::string(5000, 'w') + " model weights";
inline const std::string kVocab = "a vocabulary\n";
inline const std::string kLex = "a lexical shortlist\n";

inline std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

inline void writeAll(const std::string& path, const std::string& bytes) { std::ofstream(path, std::ios::binary) << bytes; }

inline std::string zstd(const std::string& text) {
  std::string packed(ZSTD_compressBound(text.size()), '\0');
  packed.resize(ZSTD_compress(&packed[0], packed.size(), text.data(), text.size(), 3));
  return packed;
}

inline std::string sha(const std::string& bytes) {
  const std::string path = std::string(SE_TMP) + "/store_hash_input.bin";
  writeAll(path, bytes);
  std::string error;
  return subtitle_engine::sha256File(path, &error);
}

// Each test gets an empty models directory of its own.
inline std::string freshDir(const std::string& name) {
  const std::string dir = std::string(SE_TMP) + "/store_" + name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

inline ModelFile fileOf(const std::string& role, const std::string& name, const std::string& plain, Compression c) {
  const std::string served = c == Compression::kZstd ? zstd(plain) : plain;
  return {role, name, "https://example.org/" + name, c, served.size(), plain.size(), sha(served), sha(plain)};
}

inline ModelEntry enit(const std::string& version = "3.0") {
  ModelEntry m;
  m.id = "enit";
  m.kind = ModelKind::kTranslator;
  m.sourceLanguage = "en";
  m.targetLanguage = "it";
  m.version = version;
  m.files = {fileOf("model", "model.enit.bin", kWeights, Compression::kZstd),
             fileOf("vocab", "vocab.enit.spm", kVocab, Compression::kNone),
             fileOf("lex", "lex.enit.bin", kLex, Compression::kZstd)};
  return m;
}

inline const std::map<std::string, std::string> kPlain = {
    {"model.enit.bin", kWeights}, {"vocab.enit.spm", kVocab}, {"lex.enit.bin", kLex}};

// What the caller would download: every file at the path the store asks for.
inline void stage(const ModelStore& store, const ModelEntry& m) {
  auto targets = store.downloads(m);
  ASSERT_EQ(targets.size(), m.files.size());
  for (size_t i = 0; i < targets.size(); ++i) {
    const auto& plain = kPlain.at(m.files[i].name);
    writeAll(targets[i].path, m.files[i].compression == Compression::kZstd ? zstd(plain) : plain);
  }
}

// A folder as another writer of the same models directory leaves it: the files,
// installed.json and a staging folder of its own.
inline void writeOtherWriterLayout(const std::string& dir, const ModelEntry& m) {
  fs::create_directories(dir + "/" + m.id + "/.download");
  for (const auto& f : m.files) writeAll(dir + "/" + m.id + "/" + f.name, kPlain.at(f.name));
  writeAll(dir + "/" + m.id + "/installed.json", json{{"version", m.version}}.dump());
}

