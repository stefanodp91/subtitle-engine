#pragma once
// What the files of the C interface share: strings for the caller, errors without
// subtitle text or paths, the translator handle.

#include <cstdlib>
#include <cstring>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "subtitle_engine/translator.hpp"

struct se_translator {
  std::unique_ptr<subtitle_engine::Translator> translator;
};

namespace subtitle_engine::capi {

// Subtitle text from files may not be valid UTF-8: replace bad bytes instead of throwing.
inline std::string toJson(const nlohmann::json& value) {
  return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

inline char* duplicate(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

inline const char* messageFor(const std::string& code) {
  if (code == "invalid_argument") return "invalid arguments";
  if (code == "model_load_failed") return "could not load the model files";
  if (code == "io_error") return "could not read the media";
  if (code == "not_matroska") return "the file is not a Matroska file";
  if (code == "track_not_found") return "no track with that number";
  if (code == "not_text_track") return "the track is not a text subtitle track";
  if (code == "corrupt_data") return "the data is damaged or incomplete";
  if (code == "too_large") return "the decompressed data is larger than allowed";
  if (code == "no_audio") return "the media has no audio track";
  if (code == "unsupported_codec") return "the audio codec is not supported";
  if (code == "needs_end_of_file") return "the media can only be read once its end is there";
  if (code == "decode_error") return "the audio could not be decoded";
  if (code == "store_failed") return "could not save the subtitles";
  if (code == "not_found") return "nothing saved yet";
  if (code == "other_media") return "the saved subtitles belong to another version or language";
  if (code == "other_model") return "the saved subtitles were made by another model";
  if (code == "other_format") return "the saved subtitles are in an earlier format";
  if (code == "unsupported_format") return "the catalog format is not supported";
  if (code == "no_chain") return "no translators between these languages";
  if (code == "unknown_model") return "the catalog has no model with that id";
  if (code == "not_installed") return "the model is not installed";
  if (code == "download_missing") return "a downloaded file is missing";
  if (code == "download_corrupt") return "a downloaded file does not match its hash";
  if (code == "installed_corrupt") return "an installed file does not match its hash";
  if (code == "busy") return "the model is being installed";
  return "operation failed";
}

// Error messages never carry subtitle text or file paths.
inline void setError(char** errorJson, const std::string& code, const std::string& message) {
  if (errorJson) *errorJson = duplicate(toJson(nlohmann::json{{"code", code}, {"message", message}}));
}
inline void setError(char** errorJson, const std::string& code) { setError(errorJson, code, messageFor(code)); }

}  // namespace subtitle_engine::capi
