#pragma once
// The list of models the engine works with (catalog/models.json, format 1):
// where each file is downloaded, its sizes and SHA-256 hashes.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace subtitle_engine {

enum class ModelKind { kTranslator, kSpeech, kVoiceDetector };
enum class Compression { kNone, kZstd };

/// One file of a model. role: model, lex, vocab or srcvocab and trgvocab for a
/// translator; model for the others. name is a plain file name, never a path.
struct ModelFile {
  std::string role, name, url;
  Compression compression = Compression::kNone;
  uint64_t size = 0, installedSize = 0;
  std::string sha256, installedSha256;  // 64 lowercase hex digits
};

struct ModelEntry {
  std::string id;  // also the model's folder name
  ModelKind kind = ModelKind::kTranslator;
  std::string sourceLanguage, targetLanguage;  // translators only
  std::string version, architecture, alignmentHeads;
  std::vector<ModelFile> files;

  uint64_t downloadBytes() const;
  uint64_t installedBytes() const;
};

class ModelCatalog {
 public:
  /// errorCode: invalid_argument (bad JSON or entry) or unsupported_format.
  static std::optional<ModelCatalog> parse(const std::string& json, std::string* errorCode);

  const std::vector<ModelEntry>& models() const { return models_; }
  const ModelEntry* find(const std::string& id) const;

  /// The translators from source to target: none for the same language, one
  /// pair, or two through the pivot language; nullopt when there is no way.
  std::optional<std::vector<const ModelEntry*>> chain(const std::string& source, const std::string& target) const;

 private:
  const ModelEntry* translator(const std::string& source, const std::string& target) const;

  std::string pivotLanguage_;
  std::vector<ModelEntry> models_;
};

}  // namespace subtitle_engine
