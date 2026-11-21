#include "subtitle_engine/model_store.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>

#include "model_store_files.hpp"
#include "subtitle_engine/sha256_file.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace subtitle_engine {

namespace {

// The installed.json version, or nullopt when the file is missing or unreadable.
std::optional<std::string> markerVersion(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  const json j = json::parse(std::string(std::istreambuf_iterator<char>(in), {}), nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("version") || !j["version"].is_string()) return std::nullopt;
  return j["version"].get<std::string>();
}

}  // namespace

ModelStore::ModelStore(std::string modelsDirectory) : dir_(std::move(modelsDirectory)) {}

std::string ModelStore::modelDir(const ModelEntry& model) const { return dir_ + "/" + model.id; }

std::string ModelStore::stagingDir(const ModelEntry& model) const { return modelDir(model) + "/" + kStagingFolder; }

std::string ModelStore::stagedPath(const ModelEntry& model, const ModelFile& file) const {
  return stagingDir(model) + "/" + file.name + (file.compression == Compression::kZstd ? kZstdSuffix : "");
}

std::string ModelStore::markerPath(const ModelEntry& model) const { return modelDir(model) + "/" + kMarkerFile; }

std::string ModelStore::filePath(const ModelEntry& model, const ModelFile& file) const {
  return modelDir(model) + "/" + file.name;
}

ModelStatus ModelStore::check(const ModelEntry& model, CheckMode mode) const {
  std::error_code ec;
  if (!fs::is_directory(modelDir(model), ec)) return ModelStatus::kMissing;
  const auto version = markerVersion(markerPath(model));
  if (!version) return ModelStatus::kIncomplete;
  if (*version != model.version) return ModelStatus::kOutdated;
  for (const auto& f : model.files) {
    const uintmax_t size = fs::file_size(filePath(model, f), ec);
    if (ec || size != f.installedSize) return ModelStatus::kCorrupt;
  }
  if (mode == CheckMode::kFull) {
    for (const auto& f : model.files) {
      std::string error;
      if (sha256File(filePath(model, f), &error) != f.installedSha256) return ModelStatus::kCorrupt;
    }
  }
  return ModelStatus::kInstalled;
}

std::vector<DownloadTarget> ModelStore::downloads(const ModelEntry& model) const {
  std::error_code ec;
  fs::create_directories(stagingDir(model), ec);
  std::vector<DownloadTarget> out;
  for (const auto& f : model.files) out.push_back({f.url, stagedPath(model, f), f.size});
  return out;
}

std::optional<std::string> ModelStore::translatorSpec(const std::vector<const ModelEntry*>& chain) const {
  json spec = {{"models", json::array()}};
  for (const ModelEntry* model : chain) {
    if (check(*model, CheckMode::kQuick) != ModelStatus::kInstalled) return std::nullopt;
    std::map<std::string, std::string> byRole;
    for (const auto& f : model->files) byRole[f.role] = filePath(*model, f);
    const bool split = byRole.count(kRoleSourceVocab) > 0;
    json entry = {{"model", byRole[kRoleModel]},
                  {"vocab", byRole[split ? kRoleSourceVocab : kRoleVocab]},
                  {"lex", byRole[kRoleLex]}};
    if (split) entry["target_vocab"] = byRole[kRoleTargetVocab];
    spec["models"].push_back(entry);
  }
  return spec.dump();
}

}  // namespace subtitle_engine
