#include "subtitle_engine/subtitle_store.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace subtitle_engine {

namespace {
constexpr int kFormat = 1;  // the file's format, raised when it changes
}

SubtitleStore::SubtitleStore(std::string path) : path_(std::move(path)) {}

std::optional<StoredSubtitles> SubtitleStore::load(const std::string& mediaId, const std::string& targetLanguage,
                                                   std::string* errorCode) const {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    *errorCode = "not_found";
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), {});
  const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
  StoredSubtitles out;
  try {
    if (j.is_discarded() || j.at("format").get<int>() != kFormat) throw std::runtime_error("format");
    out.mediaId = j.at("media_id").get<std::string>();
    out.targetLanguage = j.at("target_language").get<std::string>();
    out.audioLanguage = j.at("audio_language").get<std::string>();
    for (const auto& r : j.at("ranges")) out.ranges.add(r.at(0).get<int64_t>(), r.at(1).get<int64_t>());
    for (const auto& c : j.at("cues"))
      out.cues.push_back({c.at("start_ms").get<int64_t>(), c.at("end_ms").get<int64_t>(), c.at("text").get<std::string>()});
  } catch (const std::exception&) {
    *errorCode = "corrupt_data";
    return std::nullopt;
  }
  if (out.mediaId != mediaId || out.targetLanguage != targetLanguage) {
    *errorCode = "other_media";
    return std::nullopt;
  }
  return out;
}

bool SubtitleStore::save(const StoredSubtitles& s, std::string* errorCode) const {
  nlohmann::json j = {{"format", kFormat},
                      {"media_id", s.mediaId},
                      {"target_language", s.targetLanguage},
                      {"audio_language", s.audioLanguage},
                      {"ranges", nlohmann::json::array()},
                      {"cues", nlohmann::json::array()}};
  for (const auto& r : s.ranges.spans()) j["ranges"].push_back({r.first, r.second});
  for (const auto& c : s.cues) j["cues"].push_back({{"start_ms", c.startMs}, {"end_ms", c.endMs}, {"text", c.text}});
  const std::string part = path_ + ".part";
  bool ok = false;
  {
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (out) {
      out << j.dump();
      out.flush();
      ok = static_cast<bool>(out);
    }
  }
  ok = ok && std::rename(part.c_str(), path_.c_str()) == 0;
  if (!ok) {
    std::remove(part.c_str());
    *errorCode = "store_failed";
  }
  return ok;
}

}  // namespace subtitle_engine
