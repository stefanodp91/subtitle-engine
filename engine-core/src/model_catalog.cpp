#include "subtitle_engine/model_catalog.hpp"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

using nlohmann::json;

namespace subtitle_engine {

namespace {

constexpr int kFormat = 1;
constexpr size_t kHashDigits = 64;
constexpr size_t kMaxNameChars = 255;
constexpr const char* kSecureScheme = "https://";

// A rule of the format is broken: the whole catalog is refused.
struct Invalid : std::runtime_error {
  using std::runtime_error::runtime_error;
};

const std::map<std::string, ModelKind> kKinds = {
    {"translator", ModelKind::kTranslator}, {"speech", ModelKind::kSpeech}, {"voice_detector", ModelKind::kVoiceDetector}};
const std::map<std::string, Compression> kCompressions = {{"none", Compression::kNone}, {"zstd", Compression::kZstd}};
const std::vector<std::set<std::string>> kTranslatorRoles = {{"model", "vocab", "lex"},
                                                            {"model", "srcvocab", "trgvocab", "lex"}};
const std::set<std::string> kSingleModelRoles = {"model"};

std::string text(const json& j, const char* key, bool required = true) {
  auto it = j.find(key);
  if (it == j.end()) {
    if (required) throw Invalid(std::string("missing ") + key);
    return "";
  }
  if (!it->is_string() || (required && it->get<std::string>().empty())) throw Invalid(std::string("bad ") + key);
  return it->get<std::string>();
}

uint64_t positive(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_number_unsigned() || it->get<uint64_t>() == 0) throw Invalid(std::string("bad ") + key);
  return it->get<uint64_t>();
}

std::string hash(const json& j, const char* key) {
  std::string h = text(j, key);
  bool hex = std::all_of(h.begin(), h.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
  if (h.size() != kHashDigits || !hex) throw Invalid(std::string("bad ") + key);
  return h;
}

// A plain file or folder name: no separators, nothing hidden, no "..".
std::string plainName(const json& j, const char* key) {
  std::string name = text(j, key);
  if (name.size() > kMaxNameChars || name.front() == '.' || name.find("..") != std::string::npos ||
      name.find_first_of(std::string("/\\\0", 3)) != std::string::npos) {
    throw Invalid(std::string("bad ") + key);
  }
  return name;
}

template <typename T>
T pick(const std::map<std::string, T>& values, const json& j, const char* key) {
  auto it = values.find(text(j, key));
  if (it == values.end()) throw Invalid(std::string("bad ") + key);
  return it->second;
}

ModelFile parseFile(const json& j) {
  if (!j.is_object()) throw Invalid("bad file");
  ModelFile f;
  f.role = text(j, "role");
  f.name = plainName(j, "name");
  f.url = text(j, "url");
  if (f.url.rfind(kSecureScheme, 0) != 0) throw Invalid("bad url");
  f.compression = pick(kCompressions, j, "compression");
  f.size = positive(j, "size");
  f.sha256 = hash(j, "sha256");
  f.installedSize = positive(j, "installed_size");
  f.installedSha256 = hash(j, "installed_sha256");
  return f;
}

void checkRoles(const ModelEntry& m) {
  std::set<std::string> roles, names;
  for (const auto& f : m.files) {
    if (!roles.insert(f.role).second || !names.insert(f.name).second) throw Invalid("repeated file");
  }
  if (m.kind == ModelKind::kTranslator) {
    if (std::find(kTranslatorRoles.begin(), kTranslatorRoles.end(), roles) == kTranslatorRoles.end()) {
      throw Invalid("incomplete translator");
    }
  } else if (roles != kSingleModelRoles) {
    throw Invalid("bad roles");
  }
}

ModelEntry parseModel(const json& j) {
  if (!j.is_object()) throw Invalid("bad model");
  ModelEntry m;
  m.id = plainName(j, "id");
  m.kind = pick(kKinds, j, "kind");
  m.version = text(j, "version");
  m.architecture = text(j, "architecture", false);
  m.alignmentHeads = text(j, "alignment_heads", false);
  if (m.kind == ModelKind::kTranslator) {
    m.sourceLanguage = text(j, "source_language");
    m.targetLanguage = text(j, "target_language");
  }
  auto files = j.find("files");
  if (files == j.end() || !files->is_array() || files->empty()) throw Invalid("bad files");
  for (const auto& f : *files) m.files.push_back(parseFile(f));
  checkRoles(m);
  return m;
}

}  // namespace

uint64_t ModelEntry::downloadBytes() const {
  uint64_t total = 0;
  for (const auto& f : files) total += f.size;
  return total;
}

uint64_t ModelEntry::installedBytes() const {
  uint64_t total = 0;
  for (const auto& f : files) total += f.installedSize;
  return total;
}

std::optional<ModelCatalog> ModelCatalog::parse(const std::string& text, std::string* errorCode) {
  auto fail = [&](const char* code) {
    if (errorCode) *errorCode = code;
    return std::nullopt;
  };
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return fail("invalid_argument");
  auto format = j.find("format");
  if (format == j.end() || !format->is_number_integer()) return fail("invalid_argument");
  if (format->get<int>() != kFormat) return fail("unsupported_format");
  try {
    ModelCatalog catalog;
    catalog.pivotLanguage_ = subtitle_engine::text(j, "pivot_language");
    auto models = j.find("models");
    if (models == j.end() || !models->is_array()) throw Invalid("bad models");
    std::set<std::string> ids;
    for (const auto& m : *models) {
      catalog.models_.push_back(parseModel(m));
      if (!ids.insert(catalog.models_.back().id).second) throw Invalid("repeated id");
    }
    return catalog;
  } catch (const Invalid&) {
    return fail("invalid_argument");
  } catch (const json::exception&) {
    return fail("invalid_argument");
  }
}

const ModelEntry* ModelCatalog::find(const std::string& id) const {
  for (const auto& m : models_) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

const ModelEntry* ModelCatalog::translator(const std::string& source, const std::string& target) const {
  for (const auto& m : models_) {
    if (m.kind == ModelKind::kTranslator && m.sourceLanguage == source && m.targetLanguage == target) return &m;
  }
  return nullptr;
}

std::optional<std::vector<const ModelEntry*>> ModelCatalog::chain(const std::string& source,
                                                                  const std::string& target) const {
  if (source == target) return std::vector<const ModelEntry*>{};
  if (const auto* direct = translator(source, target)) return std::vector<const ModelEntry*>{direct};
  if (source == pivotLanguage_ || target == pivotLanguage_) return std::nullopt;
  const auto* first = translator(source, pivotLanguage_);
  const auto* second = translator(pivotLanguage_, target);
  if (!first || !second) return std::nullopt;
  return std::vector<const ModelEntry*>{first, second};
}

}  // namespace subtitle_engine
