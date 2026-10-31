#pragma once
// Translation of subtitle lines with Firefox Translations models.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace subtitle_engine {

/// The three files of one translation direction, as Firefox ships them.
struct ModelFiles {
  std::string model;
  std::string vocab;
  std::string lex;
};

/// One direction, or two chained through English (the first one into English).
struct TranslatorSpec {
  std::vector<ModelFiles> models;
  size_t workspaceMb = 128;
};

class Translator {
 public:
  /// Loads the models; on failure returns null and sets errorCode to
  /// "invalid_argument" or "model_load_failed".
  static std::unique_ptr<Translator> open(const TranslatorSpec& spec, std::string* errorCode);

  /// Translates each line, keeping count and order. Lines may carry <i>…</i>
  /// and '\n'; lines without visible text come back unchanged.
  std::vector<std::string> translate(const std::vector<std::string>& lines);

  ~Translator();

 private:
  struct Impl;
  explicit Translator(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace subtitle_engine
