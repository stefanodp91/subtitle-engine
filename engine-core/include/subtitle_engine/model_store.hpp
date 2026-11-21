#pragma once
// The catalog's models on disk: <models dir>/<id>/ with the installed files and
// installed.json ({"version":"…"}) written last. The engine never downloads:
// the caller saves each file where downloads() says, then calls install().

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "subtitle_engine/model_catalog.hpp"

namespace subtitle_engine {

enum class ModelStatus {
  kInstalled,
  kMissing,     // no folder
  kIncomplete,  // a folder without installed.json: an install cut short
  kOutdated,    // installed.json names another version
  kCorrupt,     // a file missing, of the wrong size or (full check) of the wrong hash
};

/// quick: marker, version and sizes; full: also the SHA-256 of every file.
enum class CheckMode { kQuick, kFull };

struct DownloadTarget {
  std::string url, path;
  uint64_t size = 0;
};

class ModelStore {
 public:
  explicit ModelStore(std::string modelsDirectory);

  ModelStatus check(const ModelEntry& model, CheckMode mode) const;

  /// Where the caller saves each file before install(), in the model's order:
  /// <dir>/<id>/.download-se/<name>, with ".zst" for compressed files. Creates
  /// that folder.
  std::vector<DownloadTarget> downloads(const ModelEntry& model) const;

  /// Checks the downloaded files, decompresses them, checks the result and
  /// replaces whatever the model's folder held. Returns "" or download_missing,
  /// download_corrupt, corrupt_data, installed_corrupt, too_large, io_error;
  /// on these the model's folder is removed. busy (another install of the same
  /// model holds <dir>/<id>.lock) touches nothing.
  std::string install(const ModelEntry& model) const;

  std::string filePath(const ModelEntry& model, const ModelFile& file) const;

  /// {"models":[{"model","vocab","lex"[,"target_vocab"]}]} for se_translator_open,
  /// or nullopt when a model of the chain is not installed (quick check).
  std::optional<std::string> translatorSpec(const std::vector<const ModelEntry*>& chain) const;

 private:
  std::string modelDir(const ModelEntry& model) const;
  std::string stagingDir(const ModelEntry& model) const;
  std::string stagedPath(const ModelEntry& model, const ModelFile& file) const;
  std::string markerPath(const ModelEntry& model) const;
  std::string installLocked(const ModelEntry& model) const;

  std::string dir_;
};

}  // namespace subtitle_engine
