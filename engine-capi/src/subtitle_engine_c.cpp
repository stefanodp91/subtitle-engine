#include "subtitle_engine_c.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "capi_common.hpp"
#include "subtitle_engine/mkv_text_tracks.hpp"
#include "subtitle_engine/translator.hpp"
#include "subtitle_engine/zstd_file.hpp"

using nlohmann::json;

using namespace subtitle_engine::capi;

extern "C" {

SE_API const char* se_version(void) { return "0.5.0"; }

SE_API void se_free(char* p) { std::free(p); }

SE_API se_translator* se_translator_open(const char* specJson, char** errorJson) {
  subtitle_engine::TranslatorSpec spec;
  try {
    json j = json::parse(specJson ? specJson : "");
    for (const auto& m : j.at("models")) {
      spec.models.push_back({m.at("model").get<std::string>(), m.at("vocab").get<std::string>(),
                             m.at("lex").get<std::string>(), m.value("target_vocab", std::string())});
    }
    if (j.contains("workspace_mb")) spec.workspaceMb = j["workspace_mb"].get<size_t>();
  } catch (const std::exception&) {
    setError(errorJson, "invalid_argument", "spec_json is not a valid translator spec");
    return nullptr;
  }
  try {
    std::string code;
    auto translator = subtitle_engine::Translator::open(spec, &code);
    if (!translator) {
      setError(errorJson, code, messageFor(code));
      return nullptr;
    }
    return new se_translator{std::move(translator)};
  } catch (...) {
    setError(errorJson, "model_load_failed", messageFor("model_load_failed"));
    return nullptr;
  }
}

SE_API char* se_translate(se_translator* translator, const char* linesJson, char** errorJson) {
  if (!translator) {
    setError(errorJson, "invalid_argument", "translator is null");
    return nullptr;
  }
  std::vector<std::string> lines;
  try {
    lines = json::parse(linesJson ? linesJson : "").get<std::vector<std::string>>();
  } catch (const std::exception&) {
    setError(errorJson, "invalid_argument", "lines_json is not an array of strings");
    return nullptr;
  }
  try {
    return duplicate(toJson(json(translator->translator->translate(lines))));
  } catch (...) {
    setError(errorJson, "translation_failed", messageFor("translation_failed"));
    return nullptr;
  }
}

SE_API void se_translator_close(se_translator* translator) { delete translator; }

namespace {

char* tracksJson(const std::vector<subtitle_engine::TextTrack>& tracks, bool withTextFlag) {
  json out = json::array();
  for (const auto& t : tracks) {
    json entry{{"number", t.number}, {"codec", t.codec}, {"language", t.language},
               {"language_bcp47", t.languageBcp47}, {"name", t.name}, {"default", t.isDefault},
               {"forced", t.isForced}, {"hearing_impaired", t.hearingImpaired}};
    if (withTextFlag) entry["text"] = t.isText;
    out.push_back(entry);
  }
  return duplicate(toJson(out));
}

}  // namespace

SE_API char* se_mkv_subtitle_tracks(const char* path, char** errorJson) try {
  std::string code;
  auto tracks = subtitle_engine::listSubtitleTracks(path ? path : "", &code);
  if (!code.empty()) {
    setError(errorJson, code, messageFor(code));
    return nullptr;
  }
  return tracksJson(tracks, true);
} catch (...) {
  setError(errorJson, "io_error", messageFor("io_error"));
  return nullptr;
}

SE_API char* se_mkv_text_tracks(const char* path, char** errorJson) try {
  std::string code;
  auto tracks = subtitle_engine::listTextTracks(path ? path : "", &code);
  if (!code.empty()) {
    setError(errorJson, code, messageFor(code));
    return nullptr;
  }
  json out = json::array();
  for (const auto& t : tracks) {
    out.push_back({{"number", t.number}, {"codec", t.codec}, {"language", t.language},
                   {"language_bcp47", t.languageBcp47}, {"name", t.name}, {"default", t.isDefault},
                   {"forced", t.isForced}, {"hearing_impaired", t.hearingImpaired}});
  }
  return duplicate(toJson(out));
} catch (...) {
  setError(errorJson, "io_error", messageFor("io_error"));
  return nullptr;
}

SE_API char* se_mkv_extract(const char* path, unsigned long long trackNumber, char** errorJson) try {
  std::string code;
  auto result = subtitle_engine::extractTextTrack(path ? path : "", trackNumber, &code);
  if (!result) {
    setError(errorJson, code, messageFor(code));
    return nullptr;
  }
  json cues = json::array();
  for (const auto& c : result->cues) cues.push_back({{"start_ms", c.startMs}, {"end_ms", c.endMs}, {"text", c.text}});
  return duplicate(toJson(json{{"complete", result->complete}, {"cues", cues}}));
} catch (...) {
  setError(errorJson, "io_error", messageFor("io_error"));
  return nullptr;
}

SE_API int se_zstd_decompress_file(const char* inPath, const char* outPath, unsigned long long maxBytes,
                                   char** errorJson) try {
  if (!inPath || !outPath) {
    setError(errorJson, "invalid_argument", messageFor("invalid_argument"));
    return 0;
  }
  const std::string code = subtitle_engine::decompressZstdFile(inPath, outPath, maxBytes);
  if (code.empty()) return 1;
  setError(errorJson, code, messageFor(code));
  return 0;
} catch (...) {
  setError(errorJson, "io_error", messageFor("io_error"));
  return 0;
}

}  // extern "C"
